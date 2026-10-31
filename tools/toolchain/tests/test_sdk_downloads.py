import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path

from toolchain import fetch, sdk_downloads
from toolchain.tests import server
from toolchain.tests.packages import sha
from toolchain.tests.test_msvc import run_cli

REPO = Path(__file__).resolve().parents[3]
TEMPLATE = "/{owner}/{repo}/{commit}/{path}"


def find_sdk():
    for candidate in (os.environ.get("FM4_SDK_ROOT"), REPO / "thirdparty" / "shiftglue-sdk",
                      REPO.parent / "pinyon-shift" / "thirdparty" / "shiftglue-sdk"):
        if candidate and (Path(candidate) / "cmake" / "rexglue_dlss.cmake").is_file():
            return Path(candidate)
    return None


def write_pins(root, dlss_commit="d" * 40, dlss=(), ffx_commit="f" * 40, ffx=(), runtime="ab" * 32):
    (root / "cmake").mkdir(parents=True, exist_ok=True)
    entries = "\n".join(f'    "{path}|{digest.lower()}"' for path, digest in dlss)
    (root / "cmake" / "rexglue_dlss.cmake").write_text(
        f"set(_rexglue_dlss_commit {dlss_commit})\nset(_rexglue_dlss_files\n{entries})\n"
        "set(_rexglue_dlss_runtime_files)\n", encoding="utf-8")
    headers = "\n".join(f'    "{path}|{digest.lower()}"' for path, digest in ffx)
    (root / "cmake" / "rexglue_ffx_api_headers.cmake").write_text(
        f"set(_rexglue_ffx_api_commit {ffx_commit})\nset(_rexglue_ffx_api_files\n{headers})\n"
        f"    set(_rexglue_ffx_runtime_hash\n        {runtime.lower()})\n", encoding="utf-8")


class SdkDownloadsTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)

    def test_read_pins_from_the_sdks_own_cmake_files(self):
        sdk = find_sdk()
        if sdk is None:
            self.skipTest("no ShiftGlue SDK checkout")
        copy = self.temp / "sdk"
        (copy / "cmake").mkdir(parents=True)
        for name in ("rexglue_dlss.cmake", "rexglue_ffx_api_headers.cmake"):
            shutil.copy(sdk / "cmake" / name, copy / "cmake" / name)
        pins = sdk_downloads.read_pins(copy)
        self.assertEqual(pins.dlss_commit, "9981c6350dc58c0ce21f024fd65e31e851c31689")
        self.assertEqual(len(pins.dlss_files), 9)
        self.assertIn(("lib/Windows_x86_64/rel/nvngx_dlss.dll",
                       "D00B5B0AB29C3CA101B70C3A5ECB3A01B7EF33766A3383A28A18623083E6123E"), pins.dlss_files)
        self.assertIn(("lib/Windows_x86_64/x64/nvsdk_ngx_s.lib",
                       "7B9A5239C1145AF5F645AB472AA066F9EA79DB36AC819562312AAE69D65F3765"), pins.dlss_files)
        self.assertEqual(pins.ffx_commit, "eee08db1688ac3d1275a70b728f4a8ba22914213")
        self.assertEqual(len(pins.ffx_headers), 4)
        self.assertEqual(pins.ffx_runtime_sha256, "A1624CC4238FEF046F30C4D80CE3F47BE63FC5F5373F49E3EE9EDB9960F54C78")

    def test_read_pins_names_the_file_missing_a_pin(self):
        write_pins(self.temp)
        text = (self.temp / "cmake" / "rexglue_dlss.cmake").read_text().replace("_rexglue_dlss_commit", "_other")
        (self.temp / "cmake" / "rexglue_dlss.cmake").write_text(text)
        with self.assertRaisesRegex(ValueError, "rexglue_dlss.cmake"):
            sdk_downloads.read_pins(self.temp)

    def serve_and_prefetch(self, served, dlss, ffx, runtime):
        write_pins(self.temp, dlss=dlss, ffx=ffx, runtime=runtime)
        pins = sdk_downloads.read_pins(self.temp)
        with server.serve(served) as base:
            sdk_downloads.prefetch(pins, self.temp / "dlss", self.temp / "build", base_urls=[base + TEMPLATE])

    def test_prefetch_puts_every_file_where_cmake_looks(self):
        header, lib, ffx, dll = b"#pragma once\n", b"lib", b"// ffx\n", b"MZ runtime"
        served = {f"/NVIDIA/DLSS/{'d' * 40}/include/a.h": header,
                  f"/NVIDIA/DLSS/{'d' * 40}/lib/x.lib": lib,
                  f"/rexglue/FidelityFX-SDK/{'f' * 40}/ffx-api/include/ffx_api/vk/ffx_api_vk.h": ffx,
                  f"/rexglue/FidelityFX-SDK/{'f' * 40}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll": dll}
        self.serve_and_prefetch(served, [("include/a.h", sha(header)), ("lib/x.lib", sha(lib))],
                                [("vk/ffx_api_vk.h", sha(ffx))], sha(dll))
        self.assertEqual((self.temp / "dlss" / "include" / "a.h").read_bytes(), header)
        self.assertEqual((self.temp / "dlss" / "lib" / "x.lib").read_bytes(), lib)
        self.assertEqual((self.temp / "build" / "ffx-api-headers" / "ffx_api" / "vk" / "ffx_api_vk.h").read_bytes(), ffx)
        self.assertEqual((self.temp / "build" / "ffx-api-headers" / "runtime" / "amd_fidelityfx_vk.dll").read_bytes(), dll)

    def test_a_dlss_header_served_with_crlf_matches_its_lf_pin(self):
        served_header = b"x\r\ny\r\n"
        dll = b"MZ"
        served = {f"/NVIDIA/DLSS/{'d' * 40}/include/a.h": served_header,
                  f"/rexglue/FidelityFX-SDK/{'f' * 40}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll": dll}
        self.serve_and_prefetch(served, [("include/a.h", sha(b"x\ny\n"))], [], sha(dll))
        self.assertEqual((self.temp / "dlss" / "include" / "a.h").read_bytes(), served_header)

    def test_a_binary_whose_bytes_differ_is_refused(self):
        served = {f"/NVIDIA/DLSS/{'d' * 40}/lib/x.lib": b"lib\r\n"}
        with self.assertRaises(fetch.DownloadError):
            self.serve_and_prefetch(served, [("lib/x.lib", sha(b"lib\n"))], [], "00" * 32)
        self.assertFalse((self.temp / "dlss" / "lib" / "x.lib").exists())

    def test_cli_prefetches_and_reports_the_dlss_folder(self):
        dll = b"MZ"
        served = {f"/NVIDIA/DLSS/{'d' * 40}/include/a.h": b"h\n",
                  f"/rexglue/FidelityFX-SDK/{'f' * 40}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll": dll}
        write_pins(self.temp, dlss=[("include/a.h", sha(b"h\n"))], runtime=sha(dll))
        with server.serve(served) as base:
            out = run_cli("sdk-downloads", "--sdk-root", self.temp, "--dlss-dir", self.temp / "dlss",
                          "--build-dir", self.temp / "build", "--base-url", base + TEMPLATE, "--json-events")
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertTrue(out.stdout.startswith("::fm4::"))
        self.assertEqual(Path(json.loads(out.stdout.splitlines()[-1])["dlss_dir"]), self.temp / "dlss")

    def test_a_failed_fidelityfx_download_keeps_the_dlss_folder(self):
        served = {f"/NVIDIA/DLSS/{'d' * 40}/include/a.h": b"h\n"}
        write_pins(self.temp, dlss=[("include/a.h", sha(b"h\n"))], runtime=sha(b"MZ"))
        with server.serve(served) as base:
            out = run_cli("sdk-downloads", "--sdk-root", self.temp, "--dlss-dir", self.temp / "dlss",
                          "--build-dir", self.temp / "build", "--base-url", base + TEMPLATE)
        self.assertEqual(out.returncode, 2)
        result = json.loads(out.stdout.splitlines()[-1])
        self.assertIn("amd_fidelityfx_vk.dll", result["message"])
        self.assertEqual(Path(result["dlss_dir"]), self.temp / "dlss")
        self.assertTrue((self.temp / "dlss" / "include" / "a.h").is_file())

    def test_a_failed_dlss_download_still_fetches_fidelityfx_and_names_no_dlss_folder(self):
        dll = b"MZ"
        served = {f"/rexglue/FidelityFX-SDK/{'f' * 40}/PrebuiltSignedDLL/amd_fidelityfx_vk.dll": dll}
        write_pins(self.temp, dlss=[("include/a.h", sha(b"h\n"))], runtime=sha(dll))
        with server.serve(served) as base:
            out = run_cli("sdk-downloads", "--sdk-root", self.temp, "--dlss-dir", self.temp / "dlss",
                          "--build-dir", self.temp / "build", "--base-url", base + TEMPLATE)
        self.assertEqual(out.returncode, 2)
        self.assertNotIn("dlss_dir", json.loads(out.stdout.splitlines()[-1]))
        self.assertTrue((self.temp / "build" / "ffx-api-headers" / "runtime" / "amd_fidelityfx_vk.dll").is_file())

    def test_cli_reports_a_failure_with_its_step(self):
        out = run_cli("sdk-downloads", "--sdk-root", self.temp, "--dlss-dir", self.temp / "dlss",
                      "--build-dir", self.temp / "build")
        self.assertEqual(out.returncode, 2)
        self.assertEqual(json.loads(out.stdout.splitlines()[-1])["error"], "sdk-downloads")

"""The SDK's configure-time downloads (DLSS, FidelityFX), fetched beforehand in parallel parts.

The pins are read from the SDK's own CMake files, and each file is checked the way that file
checks it, so CMake finds every one present and does not fetch it again.
"""
import re
from dataclasses import dataclass
from pathlib import Path

from toolchain import fetch

BASE_URLS = ["https://raw.githubusercontent.com/{owner}/{repo}/{commit}/{path}",
             # jsDelivr serves files up to 20 MB only: the DLSS runtime fails there at once.
             "https://cdn.jsdelivr.net/gh/{owner}/{repo}@{commit}/{path}"]
DLSS_CMAKE = "cmake/rexglue_dlss.cmake"
FFX_CMAKE = "cmake/rexglue_ffx_api_headers.cmake"
_ENTRY = re.compile(r'"([^"|]+)\|([0-9a-fA-F]{64})"')


@dataclass(frozen=True)
class Pins:
    dlss_commit: str
    dlss_files: list
    ffx_commit: str
    ffx_headers: list
    ffx_runtime_sha256: str
    # AMD's SDK 2.4.0 Vulkan runtime (FSR 4), from AMD's repository; empty for an SDK without it.
    ffx_sdk2_files: list = ()


def read_pins(sdk_root):
    sdk_root = Path(sdk_root)
    dlss = (sdk_root / DLSS_CMAKE).read_text(encoding="utf-8")
    ffx = (sdk_root / FFX_CMAKE).read_text(encoding="utf-8")
    return Pins(_value(dlss, "_rexglue_dlss_commit", DLSS_CMAKE),
                _entries(dlss, "_rexglue_dlss_files", DLSS_CMAKE) +
                _entries(dlss, "_rexglue_dlss_runtime_files", DLSS_CMAKE, allow_empty=True),
                _value(ffx, "_rexglue_ffx_api_commit", FFX_CMAKE),
                _entries(ffx, "_rexglue_ffx_api_files", FFX_CMAKE, allow_empty=True),
                _value(ffx, "_rexglue_ffx_runtime_hash", FFX_CMAKE).upper(),
                _entries(ffx, "_rexglue_ffx_sdk2_files", FFX_CMAKE, allow_empty=True, optional=True))


def _value(text, name, file):
    match = re.search(rf"set\(\s*{name}\s+([0-9a-fA-F]{{40,64}})\s*\)", text)
    if not match:
        raise ValueError(f"{file}: no {name}")
    return match.group(1)


def _entries(text, name, file, allow_empty=False, optional=False):
    match = re.search(rf"set\(\s*{name}\b(.*?)\)", text, re.DOTALL)
    if not match:
        if optional:
            return []
        raise ValueError(f"{file}: no {name}")
    entries = [(path, digest.upper()) for path, digest in _ENTRY.findall(match.group(1))]
    if not entries and not allow_empty:
        raise ValueError(f"{file}: {name} names no file")
    return entries


class PrefetchError(fetch.DownloadError):
    """`dlss_ready`: the DLSS folder is complete although FidelityFX failed."""

    def __init__(self, message, dlss_ready):
        super().__init__(message)
        self.dlss_ready = dlss_ready


def prefetch(pins, dlss_dir, build_dir, base_urls=BASE_URLS, progress=None):
    """Downloads what is missing or different; `progress(name, fraction)` after each step.

    A failed file does not stop the others: CMake fetches only what is still missing.
    """
    dlss_dir, build_dir = Path(dlss_dir), Path(build_dir)
    ffx_dir = build_dir / "ffx-api-headers"
    jobs = [("dlss", ("NVIDIA", "DLSS", pins.dlss_commit), path, dlss_dir / path, digest,
             # rexglue_dlss.cmake hashes its text files with LF line endings.
             path.endswith((".h", ".txt"))) for path, digest in pins.dlss_files]
    jobs += [("ffx", ("rexglue", "FidelityFX-SDK", pins.ffx_commit), f"ffx-api/include/ffx_api/{path}",
              ffx_dir / "ffx_api" / path, digest, False) for path, digest in pins.ffx_headers]
    jobs.append(("ffx", ("rexglue", "FidelityFX-SDK", pins.ffx_commit), "PrebuiltSignedDLL/amd_fidelityfx_vk.dll",
                 ffx_dir / "runtime" / "amd_fidelityfx_vk.dll", pins.ffx_runtime_sha256, False))
    jobs += [("ffx", ("GPUOpen-LibrariesAndSDKs", "FidelityFX-SDK", "v2.4.0"),
              f"Kits/FidelityFX/signedbin/{path}", ffx_dir / "runtime-2.4.0" / path, digest, False)
             for path, digest in pins.ffx_sdk2_files]
    errors, failed = [], set()
    for index, (group, (owner, repo, commit), path, destination, digest, text_lf) in enumerate(jobs):
        urls = [template.format(owner=owner, repo=repo, commit=commit, path=path) for template in base_urls]
        report = None
        if progress:
            def report(done, total, index=index, name=destination.name):
                progress(name, (index + (done / total if total else 0)) / len(jobs))
        try:
            fetch.download(urls, destination, digest, parts=16, progress=report, text_lf=text_lf)
        except fetch.DownloadError as error:
            errors.append(str(error))
            failed.add(group)
    if errors:
        raise PrefetchError("; ".join(errors), dlss_ready="dlss" not in failed)

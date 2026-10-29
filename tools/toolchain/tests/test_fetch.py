import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from toolchain import fetch
from toolchain.tests import server


def sha(data):
    return hashlib.sha256(data).hexdigest().upper()


class FetchTest(unittest.TestCase):
    DATA = bytes(range(256)) * 4000  # 1,024,000 bytes

    def setUp(self):
        patch = mock.patch.object(fetch, "_PAUSE", 0)
        patch.start()
        self.addCleanup(patch.stop)

    def test_download_splits_into_ranged_parts_and_checks_the_hash(self):
        with server.serve({"/f.bin": self.DATA}) as base, tempfile.TemporaryDirectory() as d:
            seen = []
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4,
                           progress=lambda done, total: seen.append((done, total)))
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)
            self.assertEqual(seen[-1], (len(self.DATA), len(self.DATA)))
            self.assertEqual(list(Path(d).iterdir()), [Path(d) / "f.bin"])

    def test_a_part_whose_connection_drops_resumes_where_it_stopped(self):
        with server.serve({"/f.bin": self.DATA}, cut_once={"/f.bin"}) as base, tempfile.TemporaryDirectory() as d:
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4)
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)
            # The probe, four parts and one resumed part.
            self.assertEqual(server.hits(base, "/f.bin"), 6)

    def test_a_busy_server_is_asked_again(self):
        with server.serve({"/f.bin": self.DATA}, busy_once={"/f.bin"}) as base, tempfile.TemporaryDirectory() as d:
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4)
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)

    def test_a_missing_file_is_not_asked_again(self):
        with server.serve({}) as base, tempfile.TemporaryDirectory() as d:
            with self.assertRaises(fetch.DownloadError):
                fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA))
            self.assertEqual(server.hits(base, "/f.bin"), 1)

    def test_parts_a_killed_run_left_of_another_file_are_ignored_and_removed(self):
        with server.serve({"/f.bin": self.DATA}) as base, tempfile.TemporaryDirectory() as d:
            (Path(d) / "f.bin.part0").write_bytes(b"x" * 600_000)
            (Path(d) / "f.bin.part0-of-4-999999").write_bytes(b"x" * 600_000)
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4)
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)
            self.assertEqual(list(Path(d).iterdir()), [Path(d) / "f.bin"])

    def test_a_part_a_killed_run_left_of_the_same_file_resumes(self):
        with server.serve({"/f.bin": self.DATA}) as base, tempfile.TemporaryDirectory() as d:
            (Path(d) / f"f.bin.part0-of-4-{len(self.DATA)}").write_bytes(self.DATA[:256_000])
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4)
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)
            # The probe and the three parts still missing.
            self.assertEqual(server.hits(base, "/f.bin"), 4)

    def test_server_that_ignores_range_still_gives_the_file(self):
        with server.serve({"/f.bin": self.DATA}, honour_range=False) as base, tempfile.TemporaryDirectory() as d:
            fetch.download([base + "/f.bin"], Path(d) / "f.bin", sha(self.DATA), parts=4)
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)

    def test_wrong_hash_raises_and_leaves_nothing(self):
        with server.serve({"/f.bin": self.DATA}) as base, tempfile.TemporaryDirectory() as d:
            with self.assertRaises(fetch.DownloadError):
                fetch.download([base + "/f.bin"], Path(d) / "f.bin", "00" * 32)
            self.assertEqual(list(Path(d).iterdir()), [])

    def test_mirror_is_used_after_the_first_url_fails(self):
        with server.serve({"/m.bin": self.DATA}, fail={"/f.bin"}) as base, tempfile.TemporaryDirectory() as d:
            fetch.download([base + "/f.bin", base + "/m.bin"], Path(d) / "f.bin", sha(self.DATA))
            self.assertEqual((Path(d) / "f.bin").read_bytes(), self.DATA)

    def test_present_file_with_the_right_hash_is_not_fetched(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "f.bin").write_bytes(self.DATA)
            fetch.download(["http://127.0.0.1:9/never"], Path(d) / "f.bin", sha(self.DATA))

    def test_text_hash_folds_crlf(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "a.h").write_bytes(b"x\r\ny\r\n")
            self.assertEqual(fetch.sha256_file(Path(d) / "a.h", text_lf=True), sha(b"x\ny\n"))

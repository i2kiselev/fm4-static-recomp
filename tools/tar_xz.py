"""Reproducible .tar.xz release assets: the same input files give the same bytes on every run.

As a script: python -I tools/tar_xz.py <folder> <archive.tar.xz> packs a folder's files under its
own name; python -I tools/tar_xz.py --git <archive.tar.xz> <top> <notes file> <folder>=<checkout>...
packs each git checkout's HEAD under its folder, with the notes file as README-FM4.txt.
"""
import contextlib
import io
import lzma
import subprocess
import sys
import tarfile
from pathlib import Path

MTIME = 1784764800  # package-source.ps1's fixed timestamp
# 256 MB holds a whole clang.exe, so an identical copy beside it (clang++.exe) costs almost nothing.
FILTERS = [{"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 256 << 20}]
# Source trees of hundreds of MB: the extreme preset would take many minutes for a few percent.
SOURCE_FILTERS = [{"id": lzma.FILTER_LZMA2, "preset": 9}]


def write(entries, top, archive):
    """`entries`: path below `top` -> bytes or a file's Path, written in path order."""
    with _open(archive, FILTERS) as tar:
        for name in sorted(entries):
            source = entries[name]
            info = tarfile.TarInfo(f"{top}/{name}")
            info.mtime, info.mode = MTIME, 0o644
            if isinstance(source, Path):
                info.size = source.stat().st_size
                with open(source, "rb") as file:
                    tar.addfile(info, file)
            else:
                info.size = len(source)
                tar.addfile(info, io.BytesIO(source))


def write_git(checkouts, top, archive, notes):
    """`checkouts`: (folder below `top`, git checkout) pairs, written in folder order, each its HEAD's
    files and symbolic links in git's own order; `notes` (bytes) becomes README-FM4.txt."""
    with _open(archive, SOURCE_FILTERS) as tar:
        info = tarfile.TarInfo(f"{top}/README-FM4.txt")
        info.mtime, info.mode, info.size = MTIME, 0o644, len(notes)
        tar.addfile(info, io.BytesIO(notes))
        for folder, checkout in sorted(checkouts):
            # autocrlf off: the files as committed, whatever the packing PC's git converts on checkout.
            git = subprocess.Popen(["git", "-C", str(checkout), "-c", "core.autocrlf=false", "archive",
                                    "--format=tar", "HEAD"], stdout=subprocess.PIPE)
            with tarfile.open(fileobj=git.stdout, mode="r|") as source:
                for member in source:
                    if not (member.isfile() or member.issym()):
                        continue
                    info = tarfile.TarInfo(f"{top}/{folder}/{member.name}")
                    info.mtime, info.mode = MTIME, 0o755 if member.mode & 0o111 else 0o644
                    if member.issym():
                        info.type, info.linkname = tarfile.SYMTYPE, member.linkname
                        tar.addfile(info)
                    else:
                        info.size = member.size
                        tar.addfile(info, source.extractfile(member))
            # tarfile stops at the first zero block; git still writes the rest of its padding.
            git.communicate()
            if git.returncode != 0:
                raise SystemExit(f"git archive failed in {checkout}")


@contextlib.contextmanager
def _open(archive, filters):
    archive = Path(archive)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with lzma.open(archive, "wb", format=lzma.FORMAT_XZ, filters=filters) as xz, \
            tarfile.open(fileobj=xz, mode="w|", format=tarfile.PAX_FORMAT) as tar:
        yield tar


def folder_entries(folder):
    folder = Path(folder)
    return {path.relative_to(folder).as_posix(): path for path in folder.rglob("*") if path.is_file()}


if __name__ == "__main__":
    if sys.argv[1] == "--git":
        archive, top, notes, *trees = sys.argv[2:]
        write_git([tuple(tree.split("=", 1)) for tree in trees], top, archive, Path(notes).read_bytes())
    else:
        write(folder_entries(sys.argv[1]), Path(sys.argv[1]).name, sys.argv[2])

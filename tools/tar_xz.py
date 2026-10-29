"""Reproducible .tar.xz release assets: the same input files give the same bytes on every run.

As a script: python -I tools/tar_xz.py <folder> <archive.tar.xz> packs a folder's files under its
own name.
"""
import io
import lzma
import sys
import tarfile
from pathlib import Path

MTIME = 1784764800  # package-source.ps1's fixed timestamp
# 256 MB holds a whole clang.exe, so an identical copy beside it (clang++.exe) costs almost nothing.
FILTERS = [{"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 256 << 20}]


def write(entries, top, archive):
    """`entries`: path below `top` -> bytes or a file's Path, written in path order."""
    archive = Path(archive)
    archive.parent.mkdir(parents=True, exist_ok=True)
    with lzma.open(archive, "wb", format=lzma.FORMAT_XZ, filters=FILTERS) as xz, \
            tarfile.open(fileobj=xz, mode="w|", format=tarfile.PAX_FORMAT) as tar:
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


def folder_entries(folder):
    folder = Path(folder)
    return {path.relative_to(folder).as_posix(): path for path in folder.rglob("*") if path.is_file()}


if __name__ == "__main__":
    write(folder_entries(sys.argv[1]), Path(sys.argv[1]).name, sys.argv[2])

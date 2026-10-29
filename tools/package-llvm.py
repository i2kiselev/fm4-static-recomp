"""Packs the part of LLVM the build uses into one archive, published as a release asset.

config/release-toolchain.json's llvm.package names the official Windows archive and its SHA-256,
the files kept from it (unmodified), and LLVM's licence texts, which that archive does not carry.
The archive comes out byte for byte the same on every run, so anyone can check the published one.

Usage: python -I tools/package-llvm.py [--archive <the official .tar.xz>] [--out <folder>]
"""
import argparse
import hashlib
import json
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import tar_xz  # noqa: E402
from toolchain import fetch  # noqa: E402


def main():
    parser = argparse.ArgumentParser(prog="package-llvm.py")
    parser.add_argument("--archive", type=Path, help="the official archive, if already downloaded")
    parser.add_argument("--out", type=Path, default=ROOT / ".artifacts" / "toolchain")
    args = parser.parse_args()

    llvm = json.loads((ROOT / "config" / "release-toolchain.json").read_text(encoding="utf-8"))["llvm"]
    package = llvm["package"]
    work = args.out / "work"
    source = args.archive or work / Path(package["source_url"]).name.replace("%2B", "+")
    fetch.download([package["source_url"]], source, package["source_sha256"], parts=16)

    kept = _extract(source, package["files"])
    top = Path(llvm["url"]).name.replace("%2B", "+").removesuffix(".tar.xz")
    for project, digest in package["licenses"].items():
        path = work / "licenses" / f"{project}-LICENSE.TXT"
        url = f"https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-{llvm['version']}/{project}/LICENSE.TXT"
        fetch.download([url], path, digest)
        kept[f"licenses/{project}-LICENSE.TXT"] = path.read_bytes()
    kept["README-FM4.txt"] = (
        f"A subset of the official LLVM {llvm['version']} release for 64-bit Windows\n"
        f"({package['source_url']},\nSHA-256 {package['source_sha256']}):\n"
        "only the files FM4 Static Recomp's build uses, unmodified.\n\n"
        "LLVM is licensed under the Apache License v2.0 with LLVM Exceptions; the licence texts\n"
        "of the LLVM, Clang and LLD projects are in licenses/.\n\n"
        "Made by tools/package-llvm.py of https://github.com/i2kiselev/fm4-static-recomp.\n").encode()

    result = args.out / f"{top}.tar.xz"
    tar_xz.write(kept, top, result)
    digest = hashlib.sha256(result.read_bytes()).hexdigest().upper()
    print(json.dumps({"archive": str(result), "size": result.stat().st_size, "sha256": digest,
                      "unpacked": sum(len(d) for d in kept.values())}))


def _extract(archive, files):
    """The listed files (a name ending in / keeps that folder) by their path below the top folder."""
    folders = tuple(f for f in files if f.endswith("/"))
    wanted = {f for f in files if not f.endswith("/")}
    kept = {}
    with tarfile.open(archive, mode="r|xz") as tar:
        for member in tar:
            name = member.name.split("/", 1)[1] if "/" in member.name else ""
            if not member.isfile() or not (name in wanted or name.startswith(folders)):
                continue
            kept[name] = tar.extractfile(member).read()
    missing = sorted(wanted - kept.keys()) + [f for f in folders if not any(n.startswith(f) for n in kept)]
    if missing:
        raise SystemExit(f"Not in {archive.name}: {', '.join(missing)}")
    return kept


if __name__ == "__main__":
    main()

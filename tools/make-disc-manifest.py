"""Writes config/supported-discs.json from known-good Disc 1 extractions and Disc 2 packages.

    make-disc-manifest.py --release "1.0 PAL (Europe, Australia)=1.0=D:/disc1-pal" \
        --release "Rev 1 Europe=rev1=D:/disc1-rev1" D:/disc2 config/supported-discs.json

Each release is NAME=CODE_IMAGE=DIR; the code image names the default.xex the game is
recompiled from (releases sharing one have byte-identical decrypted code). --base carries the
releases of an existing manifest without their extractions. Files identical on every release go
to disc1.files; the rest go to each release's own list. Paths compare case-insensitively (later
pressings rename some folders and files by case only); a shared file keeps the first spelling.
"""
import argparse
import hashlib
import json
import pathlib
import struct

HDR_EXECUTION_INFO = 0x00040006
# The dashboard update the console installs from the disc; the game never reads it, and some
# dumps leave it out.
OPTIONAL_PREFIX = "$systemupdate/"


def digest(path: pathlib.Path) -> str:
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest().upper()


def media_id(xex: pathlib.Path) -> str:
    raw = xex.read_bytes()
    count = struct.unpack(">I", raw[20:24])[0]
    headers = dict(struct.unpack(">II", raw[24 + i * 8:32 + i * 8]) for i in range(count))
    return raw[headers[HDR_EXECUTION_INFO]:headers[HDR_EXECUTION_INFO] + 4].hex().upper()


def release(spec: str) -> tuple[str, str, pathlib.Path]:
    name, image, root = (spec.split("=", 2) + ["", ""])[:3]
    if not name or not image or not root:
        raise argparse.ArgumentTypeError("expected NAME=CODE_IMAGE=DIR")
    return name, image, pathlib.Path(root)


def scan(root: pathlib.Path) -> dict:
    return {p.relative_to(root).as_posix().casefold():
            {"path": p.relative_to(root).as_posix(), "size": p.stat().st_size, "sha256": digest(p)}
            for p in root.rglob("*") if p.is_file()}


def carried(base: dict) -> list[tuple[dict, dict]]:
    shared = base["disc1"]["files"]
    return [(r, {f["path"].casefold(): {k: f[k] for k in ("path", "size", "sha256")}
                 for f in shared + r["files"]})
            for r in base["disc1"]["releases"]]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--release", type=release, action="append", default=[])
    parser.add_argument("--base", type=pathlib.Path)
    parser.add_argument("disc2", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    releases = []
    if args.base:
        for entry, files in carried(json.loads(args.base.read_text(encoding="utf-8"))):
            if "code_image" not in entry:
                raise SystemExit(f"{entry['name']} in {args.base} has no code_image")
            releases.append((entry["name"], entry["media_id"], entry["code_image"], files))
    for name, image, root in args.release:
        releases.append((name, media_id(root / "default.xex"), image, scan(root)))
    if not releases:
        raise SystemExit("no release given")
    if len({name for name, *_ in releases}) != len(releases):
        raise SystemExit("two releases share a name")
    spelling = {}
    for *_, files in releases:
        for key, entry in files.items():
            spelling.setdefault(key, entry["path"])
    keys = sorted(spelling, key=spelling.get)
    # Releases of one code image are the same pressing's data; another image may add files.
    for name, _, image, files in releases:
        image_keys = set().union(*(f for _, _, i, f in releases if i == image))
        absent = sorted(k for k in image_keys if k not in files and not k.startswith(OPTIONAL_PREFIX))
        if absent:
            raise SystemExit(f"{name} lacks {len(absent)} files, first {absent[0]}")
    shared, own = [], {name: [] for name, *_ in releases}
    for key in keys:
        found = [files[key] for *_, files in releases if key in files]
        optional = {"optional": True} if key.startswith(OPTIONAL_PREFIX) else {}
        if len(found) == len(releases) and all(
                (f["size"], f["sha256"]) == (found[0]["size"], found[0]["sha256"]) for f in found):
            shared.append(found[0] | optional)
        else:
            for name, _, _, files in releases:
                if key in files:
                    own[name].append(files[key] | optional)
    if any(e["path"] == "default.xex" for e in shared):
        raise SystemExit("every release has the same default.xex: one release is enough")
    packages = sorted(p for p in args.disc2.iterdir() if p.is_file())
    manifest = {
        "schema_version": 2,
        "title_id": "4D530910",
        "disc1": {
            "releases": [{"name": name, "media_id": media, "code_image": image, "files": own[name]}
                         for name, media, image, _ in releases],
            "files": shared,
        },
        "disc2": {"packages": [{"name": p.name, "size": p.stat().st_size, "sha256": digest(p)} for p in packages]},
    }
    args.output.write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8", newline="\n")
    counts = ", ".join(f"{name} {len(files)}" for name, files in own.items())
    print(f"{len(shared)} shared disc 1 files, per release: {counts}; {len(packages)} disc 2 packages")


if __name__ == "__main__":
    main()

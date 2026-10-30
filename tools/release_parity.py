"""Check that every code image still has each canonical guest address, and that they agree.

    .local/venv/Scripts/python tools/release_parity.py [--if-changed]

Without discs: every canonical address of the analysis and guest-addresses.toml has an entry
in each port table, and no entry is left without a canonical address. With the discs this PC
has (.local/releases.json: {"<release name>": "<extracted Disc 1>"}, names as in
supported-discs.json): each folder holds its release, every port entry's fingerprint or
reference count matches its image, and an entry not marked `changed` has the code the 1.0
image has at the canonical address. A missing disc or the venv's pycryptodome is reported and
skipped. --if-changed returns at once when no input changed since the last pass.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import code_images as ci  # noqa: E402

LOCAL_RELEASES = ci.ROOT / ".local/releases.json"
STAMP = ci.ROOT / ".local/generated/release-parity.stamp"


def inputs_digest() -> str:
    files = [ci.MAIN_ANALYSIS, ci.GUEST_ADDRESSES, ci.DISCS, Path(__file__), Path(ci.__file__),
             Path(ci.__file__).with_name("xex_tool.py"), *sorted(ci.PORTS.glob("*.toml"))]
    if LOCAL_RELEASES.is_file():
        files.append(LOCAL_RELEASES)
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.as_posix().encode() + b"\0" + path.read_bytes() + b"\0")
    return digest.hexdigest()


def images() -> list[str]:
    return sorted({r["code_image"] for r in ci.releases()} - {ci.CANONICAL_IMAGE})


def check_tables(problems: list[str]) -> None:
    wanted = {a.address: a for a in ci.canonical_addresses()}
    for image in images():
        table = ci.load_port_table(image)
        name = ci.port_table_path(image).relative_to(ci.ROOT).as_posix()
        problems += [f"{name}: no port for {a.label} 0x{a.address:08X}"
                     for address, a in wanted.items() if address not in table]
        problems += [f"{name}: 0x{address:08X} is no canonical address any more"
                     for address in table if address not in wanted]
        problems += [f"{name}: {wanted[address].label} 0x{address:08X} is {wanted[address].kind} "
                     f"but its entry has no {'references' if wanted[address].kind == 'data' else 'fingerprint'}"
                     for address, entry in table.items() if address in wanted and
                     ("references" if wanted[address].kind == "data" else "fingerprint") not in entry]


def discs(problems: list[str], notes: list[str]) -> dict[str, Path]:
    """One game root per code image, from the discs this PC has."""
    if not LOCAL_RELEASES.is_file():
        notes.append(f"no {LOCAL_RELEASES.relative_to(ci.ROOT).as_posix()}: disc checks skipped")
        return {}
    roots = {}
    known = {r["name"]: r["code_image"] for r in ci.releases()}
    for name, folder in json.loads(LOCAL_RELEASES.read_text(encoding="utf-8")).items():
        root = Path(folder)
        if name not in known:
            problems.append(f"{LOCAL_RELEASES.name}: {name} is no supported release")
        elif not (root / ci.EXECUTABLE).is_file():
            notes.append(f"{name}: no {ci.EXECUTABLE} in {root}, skipped")
        elif (found := ci.identify(root)).name != name:
            problems.append(f"{root} holds {found.name}, not {name}")
        else:
            roots.setdefault(known[name], root)
    for image in sorted(set(known.values()) - set(roots)):
        notes.append(f"no disc of code image {image} on this PC: its checks skipped")
    return roots


def check_images(roots: dict[str, Path], problems: list[str]) -> None:
    wanted = {a.address: a for a in ci.canonical_addresses()}
    canonical = ci.load_xex(roots[ci.CANONICAL_IMAGE]) if ci.CANONICAL_IMAGE in roots else None
    data = [a for a in wanted if wanted[a].kind == "data"]
    canonical_refs = ci.data_references(canonical, data) if canonical else {}
    for image in images():
        if image not in roots:
            continue
        xex = ci.load_xex(roots[image])
        table = {a: e for a, e in ci.load_port_table(image).items() if a in wanted}
        refs = ci.data_references(xex, [e["at"] for a, e in table.items() if wanted[a].kind == "data"])
        for address, entry in table.items():
            a = wanted[address]
            where = f"{image}: {a.label} 0x{address:08X} -> 0x{entry['at']:08X}"
            if a.kind == "data":
                if refs[entry["at"]] != entry["references"]:
                    problems.append(f"{where}: {refs[entry['at']]} references, the table says {entry['references']}")
                elif canonical and "changed" not in entry and canonical_refs[address] != entry["references"]:
                    problems.append(f"{where}: {canonical_refs[address]} references in 1.0, "
                                    f"{entry['references']} here, and no `changed` note")
                continue
            here = ci.code_fingerprint(xex, entry["at"])
            if here != entry["fingerprint"]:
                problems.append(f"{where}: the code there is not what the table fingerprinted")
            elif canonical and "changed" not in entry and (
                    ci.code_fingerprint(canonical, address) != here or
                    ci.owner_fingerprint(canonical, address) != ci.owner_fingerprint(xex, entry["at"])):
                problems.append(f"{where}: its code or owning function differs from 1.0 and it has "
                                f"no `changed` note")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--if-changed", action="store_true")
    args = ap.parse_args()
    digest = inputs_digest()
    if args.if_changed and STAMP.is_file() and STAMP.read_text(encoding="utf-8") == digest:
        return 0
    problems, notes = [], []
    check_tables(problems)
    roots = discs(problems, notes)
    if roots:
        try:
            import Crypto  # noqa: F401
        except ImportError:
            notes.append("pycryptodome is missing (use .local/venv): disc checks skipped")
            roots = {}
    check_images(roots, problems)
    for note in notes:
        print(f"note: {note}")
    if problems:
        print("release parity failed (fix with tools/port_release.py):\n  " + "\n  ".join(problems))
        return 1
    # A pass that skipped a disc proves less: the next build checks again.
    if not notes:
        STAMP.parent.mkdir(parents=True, exist_ok=True)
        STAMP.write_text(digest, encoding="utf-8")
    print(f"release parity: {len(ci.canonical_addresses())} canonical addresses, "
          f"code images checked: {', '.join(sorted(roots)) or 'none (tables only)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

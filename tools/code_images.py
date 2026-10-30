"""Code images of FM4's Disc 1 and the guest addresses the build uses in each.

Releases with byte-identical decrypted code share a code image (supported-discs.json,
`code_image`). The analysis TOMLs and config/rexglue/guest-addresses.toml hold canonical
addresses, those of the 1.0 image; config/rexglue/releases/<image>.toml (tools/port_release.py)
maps each to its place in another image, with a fingerprint of the code there.
"""
import bisect
import hashlib
import json
import struct
import tomllib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REXGLUE_CONFIG = ROOT / "config/rexglue"
MAIN_ANALYSIS = REXGLUE_CONFIG / "analysis/main-xex.toml"
GUEST_ADDRESSES = REXGLUE_CONFIG / "guest-addresses.toml"
PORTS = REXGLUE_CONFIG / "releases"
DISCS = ROOT / "config/supported-discs.json"
CANONICAL_IMAGE = "1.0"
EXECUTABLE = "default.xex"

# Instructions hashed for a code fingerprint, fewer where the owning function ends sooner.
WINDOW = 8
IMAGE_HIGH = range(0x8200, 0x8300)
D_FORM = set(range(32, 56)) | {14, 24, 25}  # loads, stores, addi, ori, oris
GPR_LOADS = {32, 33, 34, 35, 40, 41, 42, 43}  # lwz(u), lbz(u), lhz(u), lha(u)
LOAD_MULTIPLE = 46  # lmw


@dataclass(frozen=True)
class Address:
    label: str
    address: int
    kind: str  # "code" or "data"


@dataclass(frozen=True)
class Release:
    name: str
    image: str


def file_sha256(path: Path) -> str:
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest().upper()


def releases() -> list[dict]:
    return json.loads(DISCS.read_text(encoding="utf-8"))["disc1"]["releases"]


def executable_hashes(image: str) -> list[str]:
    return [f["sha256"] for r in releases() if r["code_image"] == image
            for f in r["files"] if f["path"] == EXECUTABLE]


def identify(game_root: Path) -> Release:
    """The release whose default.xex is the game root's, byte for byte."""
    xex = game_root / EXECUTABLE
    if not xex.is_file():
        raise SystemExit(f"{EXECUTABLE} is missing in {game_root}")
    found = file_sha256(xex)
    for release in releases():
        if any(f["path"] == EXECUTABLE and f["sha256"] == found for f in release["files"]):
            return Release(release["name"], release["code_image"])
    raise SystemExit(f"{xex} (SHA-256 {found}) is no supported Disc 1 release "
                     f"(config/supported-discs.json)")


def analysis_addresses(analysis: dict) -> list[tuple[str, int]]:
    """(label, address) of every address field of a parsed analysis TOML but [functions], in a
    fixed order: the same fields of two versions of one file pair up."""
    out = [(key, analysis[key]) for key in ("setjmp_address", "longjmp_address") if key in analysis]
    out += [(f"rexcrt {name}", address) for name, address in analysis.get("rexcrt", {}).items()]
    for table in analysis.get("switch_tables", []):
        out.append((f"switch 0x{table['address']:08X}", table["address"]))
        out += [(f"switch 0x{table['address']:08X} label", label) for label in table["labels"]]
    for hook in analysis.get("midasm_hook", []):
        out.append((f"hook {hook['name']}", hook["address"]))
        out += [(f"hook {hook['name']} {key}", hook[key])
                for key in ("jump_address", "jump_address_on_true", "jump_address_on_false")
                if key in hook]
    return out


def canonical_addresses() -> list[Address]:
    """Every canonical address the analysis and the C++ hooks name, but no [functions]
    fragment: those are found per code image by the discovery tools."""
    analysis = tomllib.loads(MAIN_ANALYSIS.read_text(encoding="utf-8"))
    out = [Address(label, address, "code") for label, address in analysis_addresses(analysis)]
    guest = tomllib.loads(GUEST_ADDRESSES.read_text(encoding="utf-8"))
    for kind in ("code", "data"):
        out += [Address(name, address, kind) for name, address in guest.get(kind, {}).items()]
    return out


def port_table_path(image: str) -> Path:
    return PORTS / f"{image}.toml"


def load_port_table(image: str) -> dict[int, dict]:
    path = port_table_path(image)
    if not path.is_file():
        raise SystemExit(f"code image {image} has no port table {path.relative_to(ROOT)}")
    table = tomllib.loads(path.read_text(encoding="utf-8"))
    return {int(key, 16): entry for key, entry in table.get("addresses", {}).items()}


def address_map(image: str) -> dict[int, int]:
    """Canonical -> image address for every canonical address; stops on any without an entry."""
    wanted = canonical_addresses()
    if image == CANONICAL_IMAGE:
        return {a.address: a.address for a in wanted}
    table = load_port_table(image)
    missing = [a for a in wanted if a.address not in table]
    if missing:
        raise SystemExit("no port for " + ", ".join(f"{a.label} 0x{a.address:08X}" for a in missing) +
                         f" in {port_table_path(image).relative_to(ROOT)}: run tools/port_release.py")
    return {a.address: table[a.address]["at"] for a in wanted}


def load_xex(game_root: Path):
    # xex_tool needs pycryptodome and capstone (the dev venv), which setup's Python lacks.
    from xex_tool import Xex
    return Xex(game_root / EXECUTABLE)


def words(xex, begin: int, size: int) -> list[int]:
    data = xex.read(begin, size)
    return list(struct.unpack(f">{len(data) // 4}I", data[:len(data) // 4 * 4]))


def normalise(xex, begin: int, size: int) -> list[int]:
    """The instructions with what moves between images masked: direct branches out of the
    range, and 16-bit immediates completing a lis of an image address."""
    out = []
    address_registers = set()
    for i, w in enumerate(words(xex, begin, size)):
        at = begin + i * 4
        op = w >> 26
        if op == 18:
            target = (0 if w & 2 else at) + ((w & 0x03FFFFFC) ^ 0x02000000) - 0x02000000
            if not begin <= target < begin + size or w & 1:
                w &= 0xFC000003
        elif op == 15 and (w >> 16) & 31 == 0:
            rd = (w >> 21) & 31
            if (w & 0xFFFF) in IMAGE_HIGH:
                address_registers.add(rd)
                w &= 0xFFFF0000
            else:
                address_registers.discard(rd)
        elif op in D_FORM:
            ra = (w >> 21) & 31 if op in (24, 25) else (w >> 16) & 31
            if ra in address_registers:
                w &= 0xFFFF0000
            # What a load brings is data (a pointer's fields move with the class layout).
            rd = (w >> 21) & 31
            if op in GPR_LOADS:
                address_registers.discard(rd)
            elif op == LOAD_MULTIPLE:
                address_registers -= set(range(rd, 32))
        out.append(w)
    return out


def owner(xex, address: int):
    """The PDATA function holding the address, as (begin, size), or None."""
    starts = getattr(xex, "_pdata_starts", None)
    if starts is None:
        xex._pdata_starts = starts = [b for b, _ in xex.pdata]
        xex._pdata_sizes = dict(xex.pdata)
    i = bisect.bisect_right(starts, address) - 1
    if i >= 0 and address < starts[i] + xex._pdata_sizes[starts[i]]:
        return starts[i], xex._pdata_sizes[starts[i]]
    return None


def fragment_words(xex, address: int) -> int:
    """Instructions of code without PDATA, through its first terminator past every forward
    conditional branch, at most WINDOW: what follows may be data."""
    reach = address
    for k, w in enumerate(words(xex, address, WINDOW * 4)):
        at = address + k * 4
        if w >> 26 == 16 and not w & 3:
            reach = max(reach, at + ((w & 0xFFFC) ^ 0x8000) - 0x8000)
        terminator = w in (0x4E800020, 0x4E800420) or (w >> 26 == 18 and not w & 1)
        if terminator and at >= reach:
            return k + 1
    return WINDOW


def code_fingerprint(xex, address: int) -> str:
    # Normalised over the whole owning function: a lis before the window completes addresses in it.
    if found := owner(xex, address):
        k = (address - found[0]) // 4
        window = normalise(xex, *found)[k:k + WINDOW]
    else:
        window = normalise(xex, address, fragment_words(xex, address) * 4)
    data = b"".join(w.to_bytes(4, "big") for w in window)
    return hashlib.sha256(data).hexdigest()[:16]


def owner_fingerprint(xex, address: int) -> str | None:
    """The whole owning function, normalised; None for code without PDATA."""
    if found := owner(xex, address):
        data = b"".join(w.to_bytes(4, "big") for w in normalise(xex, *found))
        return hashlib.sha256(data).hexdigest()[:16]
    return None


def data_references(xex, targets) -> dict[int, int]:
    """How many lis + D-form pairs in code build each target address."""
    targets = set(targets)
    counts = dict.fromkeys(targets, 0)
    for _, va, size, flags in xex.sections:
        if not flags & 0x20:
            continue
        high = {}
        for w in words(xex, va, size):
            op = w >> 26
            if op == 15 and (w >> 16) & 31 == 0:
                high[(w >> 21) & 31] = (w & 0xFFFF) << 16
            elif op in D_FORM and op != 25:
                ra = (w >> 21) & 31 if op == 24 else (w >> 16) & 31
                if ra in high:
                    low = w & 0xFFFF
                    if op != 24:
                        low = (low ^ 0x8000) - 0x8000
                    value = (high[ra] + low) & 0xFFFFFFFF
                    if value in targets:
                        counts[value] += 1
    return counts

"""Run codegen until validation passes, adding a function boundary for every
branch target the scanner reports as "not in any function".

A target that starts a fragment (preceded by a terminator, padding or a PDATA
function) gets its extent from a linear scan. Anything else is printed for
manual review and stops the loop. Runs on the code image of --game-root (default:
the last one resolved); default.xex's fragments go to that image's fragment list.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import release_analysis  # noqa: E402
from xex_tool import Xex  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
REXGLUE = ROOT.parent / "pinyon-shift/thirdparty/shiftglue-sdk/out/win-amd64/Release/rexglue.exe"
FACADES = {
    "SpeechFacade_default.xex": ROOT / "config/rexglue/analysis/speech-facade.toml",
    "XMediaFacade_default.xex": ROOT / "config/rexglue/analysis/xmedia-facade.toml",
}
MAX_FRAGMENT = 0x2000
ENTRY = re.compile(r"^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*size\s*=\s*0x([0-9A-Fa-f]+)", re.M)
ERROR = re.compile(r"^\s+0x([0-9A-F]{8}) from 0x([0-9A-F]{8}): .*target not in any function", re.M)


def sign_extend(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def is_terminator(word):
    if word in (0x4E800020, 0x4E800420):  # blr, bctr
        return True
    return (word >> 26) == 18 and (word & 1) == 0  # b / ba without link


def conditional_target(address, word):
    if (word >> 26) == 16 and (word & 3) == 0:
        return address + sign_extend(word & 0xFFFC, 16)
    return None


def word_at(xex, address):
    return int.from_bytes(xex.read(address, 4), "big")


def starts_fragment(xex, address, known):
    previous = address - 4
    owner = xex.function_at(previous)
    if owner or any(start <= previous < start + size for start, size in known.items()):
        return True
    word = word_at(xex, previous)
    return word == 0 or is_terminator(word)


def fragment_size(xex, start, known):
    reach = start
    address = start
    while address - start < MAX_FRAGMENT:
        if address != start:
            if xex.function_at(address):
                return address - start if address > reach else None
            # Registered interior labels of this fragment are not boundaries.
            if address in known and address > reach:
                return address - start
        word = word_at(xex, address)
        if word == 0:
            return address - start if address > reach else None
        target = conditional_target(address, word)
        if target is not None and start <= target < start + MAX_FRAGMENT:
            reach = max(reach, target)
        if is_terminator(word) and address >= reach:
            return address + 4 - start
        address += 4
    return None


def add_arguments(ap):
    ap.add_argument("--game-root", type=Path, help="extracted Disc 1 (default: the last resolved)")


def resolve(args):
    """(game root, code image, analysis TOML per module) for the parsed arguments."""
    game = (args.game_root or release_analysis.current_game_root()).resolve()
    image = release_analysis.resolve(game)
    analysis = {"default.xex": release_analysis.fragments_path(image), **FACADES}
    return game, image, analysis


def run_codegen(manifest, log):
    result = subprocess.run([str(REXGLUE), "--log-level", "info", "--log-file", str(log),
                             "codegen", str(manifest)], capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-rounds", type=int, default=60)
    add_arguments(ap)
    args = ap.parse_args()
    game, image, analysis = resolve(args)
    manifest = release_analysis.manifest_path(image)
    log = ROOT / ".local/logs/codegen.log"
    images = {}
    for round_number in range(1, args.max_rounds + 1):
        log.unlink(missing_ok=True)
        code, output = run_codegen(manifest, log)
        if code == 0:
            print(f"round {round_number}: codegen succeeded")
            print("\n".join(output.splitlines()[-25:]))
            return 0
        targets = sorted({int(t, 16) for t, _ in ERROR.findall(output)})
        module = re.findall(r"^\s+start\s+(\S+)", output, re.M)[-1]
        if not targets:
            print(f"round {round_number}: codegen failed without unresolved calls ({module})")
            print("\n".join(output.splitlines()[-40:]))
            return 1
        xex = images.setdefault(module, Xex(game / module))
        toml = analysis[module]
        text = toml.read_text(encoding="utf-8")
        known = {int(a, 16): int(s, 16) for a, s in ENTRY.findall(text)}
        added, review = [], []
        for target in targets:
            size = fragment_size(xex, target, known) if starts_fragment(xex, target, known) else None
            if size:
                added.append((target, size))
                known[target] = size
            else:
                review.append(target)
        print(f"round {round_number}: {module}: {len(targets)} unresolved, "
              f"{len(added)} added, {len(review)} need review")
        if added:
            if "[functions]" not in text:
                text = text.rstrip("\n") + "\n\n[functions]\n"
            if "# Branch targets without PDATA" not in text:
                text = text.rstrip("\n") + ("\n# Branch targets without PDATA, sized by "
                                            "tools/resolve_unresolved.py.\n")
            text = text.rstrip("\n") + "\n" + "".join(
                f"0x{a:08X} = {{ size = 0x{s:X} }}\n" for a, s in added)
            toml.write_text(text, encoding="utf-8", newline="\n")
        if review:
            for target in review:
                print(f"--- review 0x{target:08X}")
                print("\n".join(xex.disassemble(target - 0x20, 20)))
            return 2
    print("round limit reached")
    return 3


if __name__ == "__main__":
    sys.exit(main())

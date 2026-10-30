"""Register functions that are reached only through pointers in data.

Static initializers, callbacks and thunks without PDATA are invisible to the
scanner until something calls them indirectly at run time. This finds every
aligned word in .rdata/.data, and every address built by a lis/addi or lis/ori
pair in code, that points at unregistered code which starts a fragment, sizes
the fragment and appends it to the module's analysis TOML.
Run codegen first: the registered set is read from the generated init file.
Takes resolve_unresolved.py's --game-root.
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import release_analysis  # noqa: E402
from resolve_unresolved import ENTRY, add_arguments, fragment_size, is_terminator, resolve, word_at  # noqa: E402
from xex_tool import Xex  # noqa: E402

GENERATED = {
    "default.xex": "default",
    "SpeechFacade_default.xex": "speech",
    "XMediaFacade_default.xex": "xmedia",
}
LIS_WINDOW = 0x40
HEADER = "# Code pointers in .rdata/.data to fragments without PDATA, found by\n" \
         "# tools/scan_pointer_targets.py.\n"


def scan(game, image, module):
    xex = Xex(game / module)
    init = (release_analysis.image_dir(image) / GENERATED[module] / "fm4_static_recomp_init.cpp").read_text(
        encoding="utf-8", errors="replace")
    registered = {int(a, 16) for a in re.findall(r"\{\s*0x([0-9A-F]{8}),\s*\w+\s*\}", init)}
    code = [(va, va + size) for _, va, size, flags in xex.sections if flags & 0x20]
    candidates = set()
    switch_bases = set()

    def consider(value):
        if value & 3 or value in registered or xex.function_at(value):
            return
        if not any(lo <= value < hi for lo, hi in code):
            return
        previous = word_at(xex, value - 4)
        if previous == 0 or is_terminator(previous) or xex.function_at(value - 4):
            candidates.add(value)

    for name, va, size, flags in xex.sections:
        if flags & 0x20:
            # Addresses materialized in code: lis rX, hi followed shortly by
            # addi/ori on the same register.
            high = {}
            for address in range(va, va + size - 3, 4):
                word = word_at(xex, address)
                op, rd, ra = word >> 26, (word >> 21) & 31, (word >> 16) & 31
                if op == 15 and ra == 0:
                    high[rd] = ((word & 0xFFFF) << 16, address)
                elif op == 14 and ra in high and address - high[ra][1] <= LIS_WINDOW:
                    value = (high[ra][0] + ((word & 0xFFFF) ^ 0x8000) - 0x8000) & 0xFFFFFFFF
                    # A switch materializes the label after its own bctr as
                    # the jump base; that label is not a function.
                    if any(word_at(xex, value - 4) == 0x4E800420 and value - 4 == address + step
                           for step in range(4, 0x24, 4)):
                        switch_bases.add(value)
                    else:
                        consider(value)
                elif op == 24 and rd in high and address - high[rd][1] <= LIS_WINDOW:
                    consider(high[rd][0] | (word & 0xFFFF))
        elif name in (".rdata", ".data"):
            for address in range(va, va + size - 3, 4):
                consider(word_at(xex, address))
    candidates -= switch_bases
    known = dict.fromkeys(registered | candidates, 0)
    sized, unsized = [], []
    for target in sorted(candidates):
        size = fragment_size(xex, target, known)
        (sized if size else unsized).append((target, size))
    return sized, unsized, switch_bases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("modules", nargs="*", default=list(GENERATED))
    add_arguments(ap)
    args = ap.parse_args()
    game, image, analysis = resolve(args)
    for module in args.modules:
        sized, unsized, switch_bases = scan(game, image, module)
        toml = analysis[module]
        text = toml.read_text(encoding="utf-8")
        stale = [a for a in switch_bases if f"0x{a:08X} = " in text]
        for address in stale:
            text = re.sub(rf"^0x{address:08X} = .*\n", "", text, flags=re.M)
        if stale:
            print(f"{module}: removed {len(stale)} switch-base entries")
            toml.write_text(text, encoding="utf-8", newline="\n")
        present = {int(a, 16) for a, _ in ENTRY.findall(text)}
        new = [(a, s) for a, s in sized if a not in present]
        print(f"{module}: {len(sized)} pointer targets, {len(new)} new, {len(unsized)} unsized")
        for target, _ in unsized:
            print(f"  unsized 0x{target:08X}")
        if not new:
            continue
        lines = "".join(f"0x{a:08X} = {{ size = 0x{s:X} }}\n" for a, s in new)
        match = re.search(r"^\[functions\]\n", text, re.M)
        if HEADER in text:
            at = text.index(HEADER) + len(HEADER)
            text = text[:at] + lines + text[at:]
        elif match:
            lines = HEADER + lines
            text = text[:match.end()] + lines + text[match.end():]
        else:
            text = text.rstrip("\n") + "\n\n[functions]\n" + HEADER + lines
        toml.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    sys.exit(main())

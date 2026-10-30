"""Map every canonical guest address into another code image and write its port table.

    .local/venv/Scripts/python tools/port_release.py <1.0 game root> <game root>

The game roots are extracted Disc 1 folders; the second one's release (supported-discs.json)
names the code image and so config/rexglue/releases/<image>.toml. Strategies, in order:
the same offset in an identical function; an aligned offset in a changed function; an
identical function reached through the vtables pointing at a duplicated body; the most similar
function between matched neighbours (changed); for code without PDATA, the neighbours' shift
with branch targets checked through the function map; for data, the address every reference
in an identical function agrees on. Anything else stops with the address. Entries found in a
changed function carry `changed`: review them before committing the table.
"""
import argparse
import bisect
import collections
import difflib
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import code_images as ci  # noqa: E402

# Similarity a changed function needs over its runner-up to be taken as the counterpart.
MIN_RATIO = 0.9
MIN_MARGIN = 0.1
NEIGHBOURHOOD = 0x4000
VTABLE_REACH = 6


def body_hash(normalised):
    return hashlib.sha1(b"".join(w.to_bytes(4, "big") for w in normalised)).digest()


def calls(xex, begin, size):
    out = []
    for i, w in enumerate(ci.words(xex, begin, size)):
        if w >> 26 == 18:
            target = begin + i * 4 + ((w & 0x03FFFFFC) ^ 0x02000000) - 0x02000000
            if not begin <= target < begin + size:
                out.append(target)
    return out


class Port:
    def __init__(self, old, new):
        self.old, self.new = old, new
        self.fo, self.fn = dict(old.pdata), dict(new.pdata)
        self.olist, self.nlist = sorted(self.fo), sorted(self.fn)
        self.norm_o = {b: ci.normalise(old, b, s) for b, s in old.pdata}
        self.norm_n = {b: ci.normalise(new, b, s) for b, s in new.pdata}
        self.ho = {b: body_hash(w) for b, w in self.norm_o.items()}
        self.hn = {b: body_hash(w) for b, w in self.norm_n.items()}
        self.match = self._match()
        self.same = {a for a, b in self.match.items() if self.ho[a] == self.hn[b]}
        self._fragments = {}

    def _match(self):
        by_o, by_n = collections.defaultdict(list), collections.defaultdict(list)
        for b, h in self.ho.items():
            by_o[h].append(b)
        for b, h in self.hn.items():
            by_n[h].append(b)
        match = {bs[0]: by_n[h][0] for h, bs in by_o.items() if len(bs) == 1 and len(by_n.get(h, [])) == 1}
        # A matched pair's calls pair up in order.
        grew = True
        while grew:
            grew = False
            taken = set(match.values())
            for a, b in list(match.items()):
                co, cn = calls(self.old, a, self.fo[a]), calls(self.new, b, self.fn[b])
                if len(co) != len(cn):
                    continue
                for x, y in zip(co, cn):
                    if x in self.fo and y in self.fn and x not in match and y not in taken and (
                            self.fo[x] == self.fn[y] or self.ho[x] == self.hn[y]):
                        match[x] = y
                        taken.add(y)
                        grew = True
        # Between monotone anchors, functions pair up in link order.
        taken = set(match.values())
        anchors = []
        for a, b in sorted(match.items()):
            if not anchors or b > anchors[-1][1]:
                anchors.append((a, b))
        anchors = [(0, 0)] + anchors + [(0xFFFFFFFF, 0xFFFFFFFF)]
        for (a0, b0), (a1, b1) in zip(anchors, anchors[1:]):
            go = [x for x in self.olist[bisect.bisect_right(self.olist, a0):bisect.bisect_left(self.olist, a1)]
                  if x not in match]
            gn = [y for y in self.nlist[bisect.bisect_right(self.nlist, b0):bisect.bisect_left(self.nlist, b1)]
                  if y not in taken]
            if not go or not gn:
                continue
            sm = difflib.SequenceMatcher(None, [self.ho[x] for x in go], [self.hn[y] for y in gn],
                                         autojunk=False)
            for tag, i0, i1, j0, j1 in sm.get_opcodes():
                if tag == "equal" or (tag == "replace" and i1 - i0 == j1 - j0):
                    for x, y in zip(go[i0:i1], gn[j0:j1]):
                        match[x] = y
                        taken.add(y)
        return match

    def _neighbours(self, address):
        i = bisect.bisect_right(self.olist, address) - 1
        prev = next((self.olist[j] for j in range(i, max(i - 50, -1), -1) if self.olist[j] in self.match), None)
        nxt = next((self.olist[j] for j in range(i + 1, min(i + 50, len(self.olist)))
                    if self.olist[j] in self.match), None)
        return prev, nxt

    def _aligned(self, a, owner, counterpart):
        sm = difflib.SequenceMatcher(None, self.norm_o[owner], self.norm_n[counterpart], autojunk=False)
        k = (a - owner) // 4
        for tag, i0, i1, j0, j1 in sm.get_opcodes():
            if tag == "equal" and i0 <= k < i1:
                return counterpart + (j0 + k - i0) * 4
        return None

    def _through_vtables(self, owner):
        """The new function every pointer to `owner` in old data lands on, voted by the matched
        functions in the slots around it."""
        old_words = self.old.image
        new_index = getattr(self, "_new_pointers", None)
        if new_index is None:
            new_index = self._new_pointers = collections.defaultdict(list)
            for i in range(0, len(self.new.image) - 3, 4):
                w = int.from_bytes(self.new.image[i:i + 4], "big")
                if w in self.fn:
                    new_index[w].append(i)
        target = owner.to_bytes(4, "big")
        found = set()
        at = old_words.find(target)
        while at != -1:
            if at % 4 == 0:
                votes = collections.Counter()
                for k in range(-VTABLE_REACH, VTABLE_REACH + 1):
                    if k == 0 or not 0 <= at + 4 * k < len(old_words) - 3:
                        continue
                    slot = int.from_bytes(old_words[at + 4 * k:at + 4 * k + 4], "big")
                    for j in new_index.get(self.match.get(slot), []):
                        votes[j - 4 * k] += 1
                if votes:
                    j, _ = votes.most_common(1)[0]
                    found.add(int.from_bytes(self.new.image[j:j + 4], "big"))
            at = old_words.find(target, at + 1)
        if len(found) == 1:
            (candidate,) = found
            if candidate in self.fn and self.hn[candidate] == self.ho[owner]:
                return candidate
        return None

    def _most_similar(self, owner):
        prev, _ = self._neighbours(owner)
        if prev is None:
            return None
        centre = owner + self.match[prev] - prev
        lo = bisect.bisect_left(self.nlist, centre - NEIGHBOURHOOD)
        hi = bisect.bisect_right(self.nlist, centre + NEIGHBOURHOOD)
        taken = set(self.match.values())
        ranked = sorted(((difflib.SequenceMatcher(None, self.norm_o[owner], self.norm_n[b], autojunk=False).ratio(), b)
                         for b in self.nlist[lo:hi] if b not in taken), reverse=True)
        if not ranked or ranked[0][0] < MIN_RATIO or (len(ranked) > 1 and ranked[0][0] - ranked[1][0] < MIN_MARGIN):
            return None
        return ranked[0]

    def _strong(self, xex, address, count, is_old):
        out = []
        for k, w in enumerate(ci.normalise(xex, address, count * 4)):
            raw = int.from_bytes(xex.read(address + k * 4, 4), "big")
            if raw >> 26 == 18 and not raw & 2:
                target = address + k * 4 + ((raw & 0x03FFFFFC) ^ 0x02000000) - 0x02000000
                out.append(("b", raw & 3, self._code(target)[0] if is_old else target))
            else:
                out.append(w)
        return out

    def _fragment(self, a):
        """Code without PDATA: the neighbours' shift, else a unique strong hit between them."""
        if a in self._fragments:
            return self._fragments[a]
        self._fragments[a] = (None, "fragment, recursion")
        prev, nxt = self._neighbours(a)
        shifts = list(dict.fromkeys(self.match[n] - n for n in (prev, nxt) if n is not None))
        count = ci.fragment_words(self.old, a)
        want = ci.normalise(self.old, a, count * 4)
        result = None, "fragment, no counterpart between its neighbours"
        for shift in shifts:
            if ci.normalise(self.new, a + shift, count * 4) == want:
                result = a + shift, "fragment, neighbours' shift"
                break
        else:
            lo = self.match[prev] if prev is not None else a - 0x20000
            hi = self.match[nxt] + self.fn[self.match[nxt]] if nxt is not None else a + 0x20000
            strong = self._strong(self.old, a, count, True)
            hits = [b for b in range(lo, hi, 4)
                    if ci.normalise(self.new, b, count * 4) == want and self._strong(self.new, b, count, False) == strong]
            if len(hits) == 1:
                result = hits[0], "fragment, branch targets through the function map"
        self._fragments[a] = result
        return result

    def _code(self, a):
        """(new address, how, changed note or None)."""
        found = ci.owner(self.old, a)
        if found is None:
            b, how = self._fragment(a)
            return b, how, None
        owner = found[0]
        if owner in self.match:
            counterpart = self.match[owner]
            if owner in self.same:
                return counterpart + a - owner, "identical function", None
            b = self._aligned(a, owner, counterpart)
            return b, "changed function, aligned", f"owning function 0x{owner:08X} differs"
        if (counterpart := self._through_vtables(owner)) is not None:
            return counterpart + a - owner, "identical function through its vtables", None
        if similar := self._most_similar(owner):
            ratio, counterpart = similar
            b = self._aligned(a, owner, counterpart)
            return b, "most similar function, aligned", (
                f"owning function 0x{owner:08X} differs ({ratio:.1%} of it is unchanged)")
        return None, "owning function has no counterpart", None

    def data(self, targets):
        """Where the references in identical functions to each target point in the new image."""
        def refs(xex, begin, size):
            high, out = {}, []
            for i, w in enumerate(ci.words(xex, begin, size)):
                op = w >> 26
                if op == 15 and (w >> 16) & 31 == 0:
                    high[(w >> 21) & 31] = (w & 0xFFFF) << 16
                elif op in ci.D_FORM and op != 25:
                    ra = (w >> 21) & 31 if op == 24 else (w >> 16) & 31
                    if ra in high:
                        low = w & 0xFFFF if op == 24 else ((w & 0xFFFF) ^ 0x8000) - 0x8000
                        out.append((i, (high[ra] + low) & 0xFFFFFFFF))
            return out

        votes = collections.defaultdict(collections.Counter)
        targets = set(targets)
        for a in self.same:
            hits = [(i, v) for i, v in refs(self.old, a, self.fo[a]) if v in targets]
            if hits:
                new_refs = dict(refs(self.new, self.match[a], self.fn[self.match[a]]))
                for i, v in hits:
                    votes[v][new_refs.get(i)] += 1
        return votes


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("canonical_root", type=Path)
    ap.add_argument("game_root", type=Path)
    args = ap.parse_args()
    if ci.identify(args.canonical_root).image != ci.CANONICAL_IMAGE:
        raise SystemExit(f"{args.canonical_root} is not a {ci.CANONICAL_IMAGE} disc")
    image = ci.identify(args.game_root).image
    if image == ci.CANONICAL_IMAGE:
        raise SystemExit("the canonical image needs no port table")
    old, new = ci.load_xex(args.canonical_root), ci.load_xex(args.game_root)
    port = Port(old, new)
    print(f"{len(port.fo)} functions, {len(port.match)} matched, {len(port.same)} identical")

    wanted = list({a.address: a for a in ci.canonical_addresses()}.values())
    entries, failed = {}, []
    for a in (a for a in wanted if a.kind == "code"):
        b, how, changed = port._code(a.address)
        if b is None or (changed is None and ci.code_fingerprint(old, a.address) != ci.code_fingerprint(new, b)):
            failed.append(f"{a.label} 0x{a.address:08X}: {how}" + ("" if b is None else ", fingerprints differ"))
            continue
        entries[a.address] = (a, b, how, changed)
    data = [a for a in wanted if a.kind == "data"]
    votes = port.data([a.address for a in data])
    old_refs = ci.data_references(old, [a.address for a in data])
    for a in data:
        found = votes.get(a.address, collections.Counter())
        if len(found) != 1 or None in found:
            failed.append(f"{a.label} 0x{a.address:08X}: references disagree "
                          f"({', '.join(f'{v:08X}' if v else '?' for v in found) or 'none'})")
            continue
        (b,) = found
        entries[a.address] = (a, b, f"data, {found[b]} references in identical functions", None)
    if failed:
        raise SystemExit("no port for:\n  " + "\n  ".join(failed))

    new_refs = ci.data_references(new, [b for a, b, *_ in entries.values() if a.kind == "data"])
    # A reviewed note stays while the address does.
    out = ci.port_table_path(image)
    reviewed = ci.load_port_table(image) if out.is_file() else {}
    for canonical, (a, b, how, changed) in entries.items():
        if changed and reviewed.get(canonical, {}).get("at") == b and "changed" in reviewed[canonical]:
            entries[canonical] = (a, b, how, reviewed[canonical]["changed"])
    lines = [f"# Code image {image}: where each canonical (1.0) guest address of the build is in it.",
             "# Written by tools/port_release.py; `fingerprint` hashes the normalised code at `at`,",
             "# `references` counts the code building a data address. `changed` marks an address in a",
             "# function that differs from 1.0: replace its text with what a review found (kept on rerun).",
             f'image = "{image}"', "", "[addresses]"]
    for canonical in sorted(entries):
        a, b, how, changed = entries[canonical]
        check = (f"references = {new_refs[b]}" if a.kind == "data"
                 else f'fingerprint = "{ci.code_fingerprint(new, b)}"')
        note = f', changed = "{changed}"' if changed else ""
        lines.append(f"# {a.label}: {how}")
        lines.append(f"0x{canonical:08X} = {{ at = 0x{b:08X}, {check}{note} }}")
        if a.kind == "data" and new_refs[b] != old_refs[canonical]:
            print(f"note: {a.label} has {old_refs[canonical]} references in 1.0, {new_refs[b]} in {image}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    changed = sum(1 for *_, c in entries.values() if c)
    print(f"wrote {out.relative_to(ci.ROOT)}: {len(entries)} addresses, {changed} changed (review them)")


if __name__ == "__main__":
    main()

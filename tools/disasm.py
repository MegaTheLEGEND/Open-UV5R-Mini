#!/usr/bin/env python3
"""Disassemble the UV-5R Mini app.bin (JieLi AC695N, pi32v2) with cross-references.

Needs a checkout of https://github.com/jvitkauskas/pi32v2-isa (its instruction table and codec).

usage:
  python tools/disasm.py app.bin --isa ../pi32v2-isa -o build/app.dis
      [--xrefs build/xrefs.txt] [--base 0x1E00100]

Output: a linear-sweep listing with
  * branch / call targets resolved to absolute addresses (and to function labels),
  * `rN = 0x...` constants that point at a string in the image annotated with the string,
  * constants in peripheral space (0x1E0000..0x1EFFFF, below the flash window) tagged `; SFR`,
and an xref file: for every string, the functions that load its address.
"""
import argparse, os, re, sys
from collections import defaultdict

BRANCH = re.compile(r"\b(goto|call)\s+\{(\w+)\}")
IMM_LOAD = re.compile(r"^\{(\w+)\} = \{(\w+)\}$")


def load_isa(path):
    sys.path.insert(0, os.path.abspath(path))
    from pi32v2isa import table, codec
    return table.load(os.path.join(path, "isa", "pi32v2.toml")), codec


def find_strings(data, base, minlen=4):
    out = {}
    for m in re.finditer(rb"[\x20-\x7e\t\r\n]{%d,}\x00" % minlen, data):
        out[base + m.start()] = m.group()[:-1].decode("latin1")
    return out


def sweep(t, codec, data, base):
    """Yield (addr, length, insn, values) for every position of a linear sweep (insn None = undecoded)."""
    off = 0
    while off < len(data) - 1:
        got = codec.decode(t, data, off)
        if got is None:
            yield base + off, 2, None, None
            off += 2
            continue
        insn, values, n = got
        yield base + off, n, insn, values
        off += n


def branch_target(codec, insn, values, addr, n):
    m = BRANCH.search(insn.syntax)
    if not m:
        return None, None
    name = m.group(2)
    f = insn.fields.get(name)
    if f is None or f.kind != "simm":
        return None, None
    raw = values[name]
    bits = _field_width(insn, f)
    return m.group(1), addr + n + codec.value(f, raw, bits)


_width_cache = {}


def _field_width(insn, f):
    key = (insn.id, f.letters)
    if key not in _width_cache:
        _width_cache[key] = sum(w.count(c) for w in insn.words for c in f.letters)
    return _width_cache[key]


def imm_value(codec, insn, values):
    """If insn is `rN = imm` (any width), return (reg, value)."""
    m = IMM_LOAD.match(insn.syntax)
    if not m:
        return None
    f = insn.fields.get(m.group(2))
    if f is None or f.kind not in ("uimm", "simm"):
        return None
    v = codec.value(f, values[m.group(2)], _field_width(insn, f)) & 0xFFFFFFFF
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app")
    ap.add_argument("--isa", default=os.path.join(os.path.dirname(__file__), "..", "..", "pi32v2-isa"))
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x1E00100)
    ap.add_argument("-o", "--out", default="build/app.dis")
    ap.add_argument("--xrefs", default="build/xrefs.txt")
    a = ap.parse_args()

    t, codec = load_isa(a.isa)
    data = open(a.app, "rb").read()
    base = a.base
    strings = find_strings(data, base)
    items = list(sweep(t, codec, data, base))

    # pass 1: call targets become functions
    funcs = set()
    for addr, n, insn, values in items:
        if insn is None:
            continue
        kind, tgt = branch_target(codec, insn, values, addr, n)
        if kind == "call" and base <= tgt < base + len(data):
            funcs.add(tgt)
    func_sorted = sorted(funcs)

    import bisect

    def func_of(addr):
        i = bisect.bisect_right(func_sorted, addr) - 1
        return func_sorted[i] if i >= 0 else None

    def label(x):
        return f"sub_{x:08X}"

    xrefs = defaultdict(set)
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as out:
        for addr, n, insn, values in items:
            if addr in funcs:
                out.write(f"\n{label(addr)}:\n")
            raw = data[addr - base: addr - base + n].hex(" ")
            if insn is None:
                text, note = ".short 0x" + data[addr - base: addr - base + 2][::-1].hex(), ""
            else:
                text, note = codec.format(insn, values), ""
                kind, tgt = branch_target(codec, insn, values, addr, n)
                if tgt is not None:
                    note = label(tgt) if tgt in funcs else f"0x{tgt:08X}"
                v = imm_value(codec, insn, values)
                if v is not None:
                    if v in strings:
                        s = strings[v]
                        note = f"0x{v:08X} \"{s[:60]!s}\"".replace("\n", "\\n")
                        xrefs[v].add(func_of(addr))
                    elif v in funcs:
                        note = f"&{label(v)}"
                    elif 0x1E0000 <= v < 0x1F0000:
                        note = f"0x{v:06X} ; SFR"
                    elif v > 0xFFFF:
                        note = f"0x{v:08X}"
            out.write(f"{addr:08x}:  {raw:<18} {text}{'   ; ' + note if note else ''}\n")

    os.makedirs(os.path.dirname(a.xrefs) or ".", exist_ok=True)
    with open(a.xrefs, "w", encoding="utf-8") as x:
        for v in sorted(xrefs):
            fs = ", ".join(label(f) if f else "?" for f in sorted(f for f in xrefs[v] if f) )
            x.write(f"0x{v:08X} {strings[v][:50]!r:56s} <- {fs}\n")
    print(f"{len(items)} items, {len(funcs)} functions, {len(xrefs)} referenced strings")


if __name__ == "__main__":
    main()

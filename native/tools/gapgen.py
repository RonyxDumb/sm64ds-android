#!/usr/bin/env python3
"""The parts of the DS image that the decomp has no source for, and how the native port fills them.

The decomp's delinks.txt files give every module's sections and the source units that own address ranges in them.
Ranges no unit owns ("gaps": mostly data the decomp keeps as extracted bytes) stay at their DS address in the
reserved DS address space and are filled from the user's ROM when the module is loaded (port/platform/image.c):

  - every symbol inside a data/rodata/bss gap becomes an absolute symbol at its DS address (gap_symbols.S), so the
    game's code links against the ROM's bytes in place;
  - every pointer inside a gap whose target is native (code, or data a source unit defines) is listed in a
    relocation table (gap_relocs.S): after copying the ROM bytes the loader stores the native address there.
    Pointers from gap to gap stay as they are: both sides keep their DS addresses.

Symbols in a *code* gap (functions without source) cannot run from the ROM: they are written to code_gaps.txt and
must be implemented natively (port/platform).

No ROM content is read or written here: only the decomp's configuration.

Usage: gapgen.py <decomp> <out dir> [--native-defined FILE]  (FILE: names the native build defines, one per line)
"""
import bisect
import re
import sys
from collections import defaultdict
from pathlib import Path

SEC_RE = re.compile(r"\s*(\.\w+)\s+start:(0x[0-9a-fA-F]+)\s+end:(0x[0-9a-fA-F]+)(?:\s+kind:(\w+))?")
SYM_RE = re.compile(r"^(\S+)\s+kind:(\w+)(?:\(([^)]*)\))?\s+addr:(0x[0-9a-fA-F]+)")
REL_RE = re.compile(r"from:(0x[0-9a-fA-F]+)\s+kind:(\w+)\s+to:(0x[0-9a-fA-F]+)(?:\s+add:(-?0x[0-9a-fA-F]+|-?\d+))?\s+module:(\S+)")


class Module:
    def __init__(self, name, cfg):
        self.name, self.cfg = name, cfg
        self.sections = {}      # name -> (start, end, kind)
        self.owned = []         # (start, end)
        self.symbols = []       # (addr, size, name, kind)
        self.relocs = []        # (from, kind, to, addend, module spec)
        self.read()

    def read(self):
        cur = None
        for line in open(self.cfg / "delinks.txt"):
            if not line.strip():
                continue
            if not line.startswith((" ", "\t")):
                cur = line.strip()[:-1]
                continue
            m = SEC_RE.match(line)
            if not m:
                continue
            a, b = int(m.group(2), 16), int(m.group(3), 16)
            if cur is None:
                self.sections[m.group(1)] = (a, b, m.group(4) or "")
            elif b > a:
                self.owned.append((a, b))
        self.owned.sort()
        for line in open(self.cfg / "symbols.txt"):
            m = SYM_RE.match(line)
            if not m:
                continue
            attrs = m.group(3) or ""
            size = 0
            sm = re.search(r"size=(0x[0-9a-fA-F]+|\d+)", attrs)
            if sm:
                size = int(sm.group(1), 0)
            self.symbols.append((int(m.group(4), 16), size, m.group(1), m.group(2), attrs))
        self.symbols.sort()
        self.sym_addrs = [s[0] for s in self.symbols]
        rel = self.cfg / "relocs.txt"
        if rel.exists():
            for line in open(rel):
                m = REL_RE.search(line)
                if m:
                    add = int(m.group(4), 0) if m.group(4) else 0
                    self.relocs.append((int(m.group(1), 16), m.group(2), int(m.group(3), 16), add, m.group(5)))
        self.owned_starts = [a for a, _ in self.owned]

    def is_owned(self, addr):
        i = bisect.bisect_right(self.owned_starts, addr) - 1
        return i >= 0 and self.owned[i][0] <= addr < self.owned[i][1]

    def section_of(self, addr):
        for n, (a, b, k) in self.sections.items():
            if a <= addr < b:
                return n, k
        return None, None

    def containing_symbol(self, addr):
        i = bisect.bisect_right(self.sym_addrs, addr) - 1
        # Prefer the closest symbol at or before addr that spans it (size 0: assume it does).
        while i >= 0:
            a, size, name, kind, _ = self.symbols[i]
            if a == addr or (size and a <= addr < a + size) or (not size and a <= addr):
                return self.symbols[i]
            i -= 1
        return None


def modules(decomp):
    cfg = Path(decomp) / "config" / "arm9"
    mods = {"main": Module("main", cfg)}
    for name in ("itcm", "dtcm"):
        if (cfg / name / "delinks.txt").exists():
            mods[name] = Module(name, cfg / name)
    for d in sorted((cfg / "overlays").iterdir()):
        mods[d.name] = Module(d.name, d)
    return mods


def target_modules(spec, mods, src):
    if spec in ("main", "itcm", "dtcm"):
        return [mods[spec]]
    m = re.match(r"overlays?\(([\d,]+)\)", spec)
    if m:
        return [mods[f"ov{int(i):03d}"] for i in m.group(1).split(",") if f"ov{int(i):03d}" in mods]
    if spec == "none":
        return []
    return [src]


def main():
    decomp, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    mods = modules(decomp)

    abs_syms = {}           # name -> addr (gap data symbols)
    code_gaps = []
    gap_ranges = defaultdict(list)  # module -> [(start, end, kind)]
    for m in mods.values():
        for sname, (a, b, kind) in m.sections.items():
            # Uncovered ranges of the section.
            pos = a
            for oa, ob in m.owned:
                if ob <= a or oa >= b:
                    continue
                if oa > pos:
                    gap_ranges[m.name].append((pos, oa, kind, sname))
                pos = max(pos, ob)
            if pos < b:
                gap_ranges[m.name].append((pos, b, kind, sname))
        for addr, size, name, kind, attrs in m.symbols:
            if m.is_owned(addr):
                continue
            sec, skind = m.section_of(addr)
            if sec is None:
                continue
            if kind == "function" or skind == "code":
                code_gaps.append((m.name, addr, size, name, sec))
            elif kind in ("data", "bss", "label"):
                if name in abs_syms and abs_syms[name] != addr:
                    print(f"gapgen: {name} at two addresses ({abs_syms[name]:#x}, {addr:#x}), keeping the first",
                          file=sys.stderr)
                    continue
                abs_syms[name] = addr

    # Relocations stored inside data gaps whose target is native.
    patch = defaultdict(list)   # module -> [(from, symbol, addend)]
    unresolved = []
    for m in mods.values():
        for frm, kind, to, add, spec in m.relocs:
            if kind != "load" or m.is_owned(frm):
                continue
            sec, skind = m.section_of(frm)
            if skind == "code" or sec is None:
                continue
            target = None
            for tm in target_modules(spec, mods, m):
                if tm.section_of(to)[0] is None:
                    continue
                if not tm.is_owned(to) and tm.section_of(to)[1] != "code":
                    target = "gap"
                    break
                s = tm.containing_symbol(to)
                if s:
                    target = (s[2], to - s[0])
                    break
            if target == "gap":
                continue
            if target is None:
                unresolved.append((m.name, frm, to, spec))
                continue
            patch[m.name].append((frm, target[0], target[1] + add))

    s = ["/* Generated by tools/gapgen.py: data the decomp has no source for, at its DS address. */"]
    for name, addr in sorted(abs_syms.items(), key=lambda x: x[1]):
        s.append(f'.globl "{name}"\n.set "{name}", {addr:#010x}')
    s.append('.section .note.GNU-stack,"",%progbits')
    (out / "gap_symbols.S").write_text("\n".join(s) + "\n")

    r = ["/* Generated by tools/gapgen.py: native addresses stored into the gap data of each module after it is",
         "   copied from the ROM (port/platform/image.c). Per module: a count, then (DS address, native address). */",
         ".section .rodata.gap_relocs, \"a\"", ".balign 4"]
    for name in sorted(mods):
        entries = patch.get(name, [])
        r.append(f".globl port_gap_relocs_{name}\nport_gap_relocs_{name}:\n  .4byte {len(entries)}")
        for frm, sym, add in entries:
            r.append(f'  .4byte {frm:#010x}, "{sym}" + {add}' if add else f'  .4byte {frm:#010x}, "{sym}"')
    r.append('.section .note.GNU-stack,"",%progbits')
    (out / "gap_relocs.S").write_text("\n".join(r) + "\n")

    with open(out / "gap_ranges.txt", "w") as f:
        for name in sorted(gap_ranges):
            for a, b, kind, sec in gap_ranges[name]:
                f.write(f"{name} {sec} {kind} {a:#010x} {b:#010x}\n")
    with open(out / "code_gaps.txt", "w") as f:
        for mod, addr, size, name, sec in code_gaps:
            f.write(f"{mod} {addr:#010x} {size:#x} {sec} {name}\n")
    with open(out / "unresolved_relocs.txt", "w") as f:
        for mod, frm, to, spec in unresolved:
            f.write(f"{mod} {frm:#010x} -> {to:#010x} {spec}\n")
    total_gap = sum(b - a for v in gap_ranges.values() for a, b, k, s in v if k != "code")
    print(f"gapgen: {len(abs_syms)} gap symbols, {sum(len(v) for v in patch.values())} native pointers in gaps, "
          f"{len(code_gaps)} code-gap symbols, {len(unresolved)} unresolved, {total_gap} bytes of gap data")


if __name__ == "__main__":
    main()

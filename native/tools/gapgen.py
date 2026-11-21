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

Writes OUT/symbols.ld (a linker script of symbol assignments: data without source at its DS address, labels inside
storage objects, the decomp's wrapper aliases, linker-generated symbols) and OUT/gap_relocs.S.

Usage: gapgen.py <decomp> <out dir> --defined FILE  (FILE: the symbols the native objects define, one per line)
"""
import argparse
import bisect
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

SEC_RE = re.compile(r"\s*(\.[\w.]+)\s+start:(0x[0-9a-fA-F]+)\s+end:(0x[0-9a-fA-F]+)(?:\s+kind:(\w+))?")
SYM_RE = re.compile(r"^(\S+)\s+kind:(\w+)(?:\(([^)]*)\))?\s+addr:(0x[0-9a-fA-F]+)")
REL_RE = re.compile(r"from:(0x[0-9a-fA-F]+)\s+kind:(\w+)\s+to:(0x[0-9a-fA-F]+)(?:\s+add:(-?0x[0-9a-fA-F]+|-?\d+))?\s+module:(\S+)")


EXCLUDED = []  # units the native build does not compile (port/exclude.txt): their ranges are gaps


def excluded(unit):
    return any(unit == e or (e.endswith("/") and unit.startswith(e)) for e in EXCLUDED)


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
            elif b > a and not excluded(cur):
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


def unit_of(m, addr):
    if not hasattr(m, "_units"):
        m._units = []
        cur = None
        for line in open(m.cfg / "delinks.txt"):
            if not line.strip():
                continue
            if not line.startswith((" ", "\t")):
                cur = line.strip()[:-1]
                continue
            mm = SEC_RE.match(line)
            if mm and cur:
                m._units.append((int(mm.group(2), 16), int(mm.group(3), 16), cur))
        m._units.sort()
        m._ustarts = [u[0] for u in m._units]
    i = bisect.bisect_right(m._ustarts, addr) - 1
    if i >= 0 and m._units[i][0] <= addr < m._units[i][1]:
        return m._units[i][2]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("decomp")
    ap.add_argument("out")
    ap.add_argument("--defined", help="file: the symbols the native objects define, one per line")
    ap.add_argument("--exclude", help="port/exclude.txt: units the native build does not compile")
    args = ap.parse_args()
    if args.exclude:
        for line in open(args.exclude):
            line = line.split("#", 1)[0].strip()
            if line:
                EXCLUDED.append(line)
    decomp, out = Path(args.decomp), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    mods = modules(decomp)
    defined = set()
    if args.defined:
        defined = set(open(args.defined).read().split())

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
        native = sorted((a, n) for a, size, n, kind, attrs in m.symbols if n in defined)
        nstarts = [a for a, n in native]
        for frm, kind, to, add, spec in m.relocs:
            if kind != "load":
                continue
            where = frm
            if m.is_owned(frm):
                # A pointer in data a source unit defines: its native place is the native object holding it, at the
                # same offset (the compiler already put a native pointer there unless the source spells a DS address).
                if not defined:
                    continue
                i = bisect.bisect_right(nstarts, frm) - 1
                if i < 0 or unit_of(m, native[i][0]) != unit_of(m, frm):
                    continue
                where = (native[i][1], frm - native[i][0])
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
            patch[m.name].append((where, target[0], target[1] + add, to + add))
    aliases = {}
    for f in sorted((Path(decomp) / "config" / "tu_manifest.d").glob("*/*.json")):
        try:
            d = json.load(open(f))
        except ValueError:
            continue
        if isinstance(d, dict):
            for a in d.get("undefined_symbol_aliases") or []:
                aliases.setdefault(a["symbol"], a["canonical_symbol"])

    ld = ["/* Generated by tools/gapgen.py from the decomp's configuration: symbols the native objects do not define. */"]
    def q(n):
        return '"' + n + '"'
    emitted = set()
    # 1. Data without source: at its DS address.
    for name, addr in sorted(abs_syms.items(), key=lambda x: x[1]):
        if name not in defined:
            ld.append(f"{q(name)} = {addr:#010x};")
            emitted.add(name)
    # 2. Labels inside a storage object a source unit defines: the object's native address plus the offset.
    labels = 0
    for m in mods.values():
        native = [(a, n) for a, size, n, kind, attrs in m.symbols if n in defined]
        starts = [a for a, n in native]
        for addr, size, name, kind, attrs in m.symbols:
            if name in defined or name in emitted or not m.is_owned(addr) or kind == "function":
                continue
            i = bisect.bisect_right(starts, addr) - 1
            if i < 0:
                continue
            base_addr, base = native[i]
            if not unit_of(m, base_addr) or unit_of(m, base_addr) != unit_of(m, addr):
                continue
            ld.append(f"{q(name)} = {q(base)} + {addr - base_addr:#x};")
            emitted.add(name)
            labels += 1
    # 3. Wrapper constructors/destructors the decomp's build aliases to their shared implementation.
    for name, canon in sorted(aliases.items()):
        if name in defined or name in emitted:
            continue
        if canon not in defined and canon not in emitted:
            # The shared implementation is an inline body the native compiler did not emit: a complete-object
            # destructor of a type without one (Vector3s, ...) does nothing.
            if re.search(r"D[12]Ev$", canon):
                canon = "port_trivial_dtor"
            else:
                continue
        ld.append(f"PROVIDE({q(name)} = {q(canon)});")
        emitted.add(name)
    # 4. Linker-generated symbols: overlay ids and section bounds, at their DS values.
    for name, m in mods.items():
        if name.startswith("ov"):
            n = int(name[2:])
            ld.append(f"PROVIDE(OVERLAY_{n}_ID = {n});")
            for sname, (a, b, kind) in m.sections.items():
                if re.fullmatch(r"\.(text|rodata|data|bss|init|ctor)", sname):
                    t = sname[1:].upper()
                    ld.append(f"PROVIDE(OV{n:03d}_{t}_START = {a:#010x});")
                    ld.append(f"PROVIDE(OV{n:03d}_{t}_END = {b:#010x});")
    main = mods["main"]
    for sname, (a, b, kind) in main.sections.items():
        if re.fullmatch(r"\.(text|rodata|data|bss|init|ctor)", sname):
            t = sname[1:].upper()
            ld.append(f"PROVIDE(ARM9_{t}_START = {a:#010x});")
            ld.append(f"PROVIDE(ARM9_{t}_END = {b:#010x});")
    if "dtcm" in mods:
        ld.append(f"PROVIDE(SDK_AUTOLOAD_DTCM_START = {min(a for a, b, k in mods['dtcm'].sections.values()):#010x});")
    # 5. Base-object structors: mwcc emits only the complete-object ones (D1, C1), GCC's derived classes call D2/C2.
    for name in sorted(defined):
        m = re.match(r"(_ZN.*)([DC])1(E.*)$", name)
        if m:
            other = m.group(1) + m.group(2) + "2" + m.group(3)
            if other not in defined and other not in emitted:
                ld.append(f"PROVIDE({q(other)} = {q(name)});")
    (out / "symbols.ld").write_text("\n".join(ld) + "\n")
    print(f"gapgen: {labels} storage labels, {len(aliases)} aliases")

    r = ["/* Generated by tools/gapgen.py: native addresses stored into the gap data of each module after it is",
         "   copied from the ROM (port/platform/image.c). Per module: a count, then (place, native address, the DS address",
         "   the place holds before it is redirected). */",
         ".section .rodata.gap_relocs, \"a\"", ".balign 4"]
    dropped = []
    for name in sorted(mods):
        entries = patch.get(name, [])
        r.append(f".globl port_gap_relocs_{name}\nport_gap_relocs_{name}:\n  .4byte {len(entries)}")
        skipped = [e for e in entries if args.defined and e[1] not in defined and e[1] not in emitted]
        entries = [e for e in entries if not (args.defined and e[1] not in defined and e[1] not in emitted)]
        dropped.extend((name, e) for e in skipped)
        r[-1] = f".globl port_gap_relocs_{name}\nport_gap_relocs_{name}:\n  .4byte {len(entries)}"
        for frm, sym, add, ds in entries:
            place = f'"{frm[0]}" + {frm[1]:#x}' if isinstance(frm, tuple) else f"{frm:#010x}"
            target = f'"{sym}" + {add}' if add else f'"{sym}"'
            r.append(f"  .4byte {place}, {target}, {ds:#010x}")
    r.append('.section .note.GNU-stack,"",%progbits')
    (out / "gap_relocs.S").write_text("\n".join(r) + "\n")

    # Pointers to code or data the native build has no symbol for keep their DS address (the ROM's bytes there).
    with open(out / "dropped_relocs.txt", "w") as f:
        for mod, (frm, sym, add, ds) in dropped:
            f.write(f"{mod} {frm} {sym}+{add:#x}\n")
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

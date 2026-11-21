#!/usr/bin/env python3
"""Prepare the decomp's sources (native/decomp, unmodified) for the native port.

For every source unit the real build links (config/arm9/**/delinks.txt) that the port compiles, write a copy under
OUT/src/ that clang/GCC can build:
  - port/patches/<path>.patch (unified diff against the decomp file) is applied when present;
  - writes to registers with side effects (`REG_GXFIFO = v;`, `REG_VRAMCNT_A |= v;` ...) become
    PORT_IO_WRITE(REG_X, v), which forwards them to the geometry engine, the VRAM mapper or the interrupt controller.

Headers: one tree, the decomp's include/ with the port's shadow headers (port/compat/include) on top; a shadow header
reaches the decomp's own with `#include_next "same/name.h"`.

Writes OUT/sources.txt (one generated path per line) and OUT/overlay_units.txt ("ovNNN path" per overlay unit).
Files are only rewritten when their content changes, so incremental builds stay incremental.

Usage: portify.py <decomp> <out> <exclude-list>
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent / "port"

# Registers whose writes have side effects. A statement `REG_X op= expr;` is rewritten to PORT_IO_WRITE.
SIDE_EFFECT = r"REG_(?:GXFIFO|VRAMCNT_[A-I]\w*|IF|MTX_\w+|VTX_\w+|COLOR|NORMAL|TEXCOORD|BEGIN_VTXS|" \
              r"END_VTXS|SWAP_BUFFERS|VIEWPORT|BOX_TEST|POS_TEST|VEC_TEST|POLYGON_ATTR|TEXIMAGE_PARAM|PLTT_BASE|" \
              r"DIF_AMB|SPE_EMI|LIGHT_VECTOR|LIGHT_COLOR|SHININESS)"
REG_WRITE = re.compile(r"(?<![\w.>])(" + SIDE_EFFECT + r")\s*([|&^]?)=(?!=)\s*([^;]+);")


def rewrite_reg_writes(text):
    def sub(m):
        reg, op, value = m.group(1), m.group(2), m.group(3)
        if op:
            value = f"{reg} {op} ({value})"
        return f"PORT_IO_WRITE({reg}, {value});"
    return REG_WRITE.sub(sub, text)


# A variable declared `extern` inside a function. mwcc lets two such declarations of one symbol disagree on its type
# (or on its linkage, or with the symbol's definition later in the file); clang and GCC do not. Each one becomes a
# declaration of a name of its own bound to the same symbol by an assembler label, and the name is renamed in the
# rest of its block: the code reads the symbol with exactly the type that declaration gives it.
BLOCK_EXTERN = re.compile(r"^(\s+)extern\s+(?!\"C)([^;(){}=,]*?)\b([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*);(.*)$")
# The same for a pointer to a function or member function: extern R (C::*name[N])(args);
BLOCK_EXTERN_FN = re.compile(r"^(\s+)extern\s+(?!\"C)([^;{}=]*?\(\s*(?:[\w:]+::)?\*\s*)([A-Za-z_]\w*)((?:\s*\[[^\]]*\])*\s*\)\s*\([^;{}]*\));(.*)$")


def rewrite_block_externs(text):
    lines = text.split("\n")
    out = list(lines)
    stack = []  # one entry per open brace: True for a function-level block, False for extern "C" / namespace
    count = 0
    for i, line in enumerate(lines):
        in_function = any(stack)
        m = (BLOCK_EXTERN.match(line) or BLOCK_EXTERN_FN.match(line)) if in_function else None
        if m and m.group(2).strip() and not m.group(5).strip().startswith(("\\",)):
            indent, type_, name, dims, rest = m.groups()
            count += 1
            alias = f"{name}__x{count}"
            out[i] = f'{indent}extern {type_}{alias}{dims} __asm__("{name}");{rest}'
            # Rename in the rest of the enclosing block.
            d = 0
            word = re.compile(r"\b%s\b" % re.escape(name))
            for j in range(i + 1, len(lines)):
                code = lines[j].split("//", 1)[0]
                d += code.count("{") - code.count("}")
                out[j] = word.sub(alias, out[j])
                if d < 0:
                    break
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", line.split("//", 1)[0])
        linkage = bool(re.match(r'\s*(extern\s+"C(\+\+)?"|namespace\b)', code))
        for ch in code:
            if ch == "{":
                stack.append(not linkage)
                linkage = False
            elif ch == "}" and stack:
                stack.pop()
    return "\n".join(out)


def delinks_files(decomp):
    cfg = Path(decomp) / "config" / "arm9"
    files = [("main", cfg / "delinks.txt"), ("itcm", cfg / "itcm" / "delinks.txt"), ("dtcm", cfg / "dtcm" / "delinks.txt")]
    files += [(d.name, d / "delinks.txt") for d in sorted((cfg / "overlays").iterdir())]
    return [(m, f) for m, f in files if f.exists()]


def units_of(path):
    for line in open(path):
        line = line.rstrip()
        if line and not line.startswith((" ", "\t", "/")) and line.endswith(":"):
            yield line[:-1]


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        if path.read_text(encoding="utf-8", errors="surrogateescape") == text:
            return
    except OSError:
        pass
    path.write_text(text, encoding="utf-8", errors="surrogateescape")


def apply_patch(text, patch, rel):
    with tempfile.TemporaryDirectory() as tmp:
        f = Path(tmp) / "file"
        f.write_text(text, encoding="utf-8", errors="surrogateescape")
        r = subprocess.run(["patch", "-s", "--no-backup-if-mismatch", str(f), str(patch)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"portify: {patch} does not apply to {rel}:\n{r.stdout}{r.stderr}")
        return f.read_text(encoding="utf-8", errors="surrogateescape")


def main():
    decomp, out, exclude_file = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), sys.argv[3]
    excludes = []
    for line in open(exclude_file):
        line = line.split("#", 1)[0].strip()
        if line:
            excludes.append(line)

    def excluded(rel):
        return any(rel == e or (e.endswith("/") and rel.startswith(e)) for e in excludes)

    module_of = {}
    for module, f in delinks_files(decomp):
        for u in units_of(f):
            module_of.setdefault(u, module)

    patches = PORT / "patches"
    sources = []
    for rel in sorted(module_of):
        if not rel.endswith((".c", ".cpp")) or excluded(rel):
            continue
        text = (decomp / rel).read_text(encoding="utf-8", errors="surrogateescape")
        patch = patches / (rel + ".patch")
        if patch.exists():
            text = apply_patch(text, patch, rel)
        text = rewrite_block_externs(rewrite_reg_writes(text))
        dst = out / "src" / rel
        write_if_changed(dst, f'#line 1 "{rel}"\n' + text)
        sources.append(dst.as_posix())

    inc = out / "include"
    compat = PORT / "compat" / "include"
    shadowed = {p.relative_to(compat).as_posix() for p in compat.rglob("*.h")}
    for h in sorted((decomp / "include").rglob("*.h")):
        rel = h.relative_to(decomp / "include").as_posix()
        text = h.read_text(encoding="utf-8", errors="surrogateescape")
        patch = patches / "include" / (rel + ".patch")
        if patch.exists():
            text = apply_patch(text, patch, rel)
        if rel == "types.h":
            text, n = re.subn(r"^typedef unsigned long size_t;$", "typedef __SIZE_TYPE__ size_t;", text, flags=re.M)
            if n != 1:
                sys.exit("portify: types.h: size_t typedef not found")
            # mwcc aligns 64-bit integers to 4 bytes (u64 members sit at offsets like 0x2c); keep that layout.
            for t in ("u64", "s64", "fx64", "fx64c"):
                text, n = re.subn(rf"^typedef ([^;]*) {t};$", rf"typedef \1 __attribute__((aligned(4))) {t};", text,
                                  flags=re.M)
                if n != 1:
                    sys.exit(f"portify: types.h: typedef of {t} not found")
        if rel != "nitro/hw/registers.h":
            text = rewrite_reg_writes(text)
        if rel in shadowed:
            rel += ".decomp"
        write_if_changed(inc / rel, text)
    for h in sorted(compat.rglob("*.h")):
        rel = h.relative_to(compat).as_posix()
        text = re.sub(r'#include_next "([^"]+)"', r'#include "\1.decomp"', h.read_text())
        write_if_changed(inc / rel, text)

    write_if_changed(out / "sources.txt", "\n".join(sources) + "\n")
    write_if_changed(out / "overlay_units.txt",
                     "".join(f"{module_of[r]} {r}\n" for r in sorted(module_of)
                             if module_of[r].startswith("ov") and (out / "src" / r).exists()))


if __name__ == "__main__":
    main()

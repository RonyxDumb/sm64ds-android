#!/usr/bin/env python3
"""Audited Android-only C++ projections for individually reviewed raw source units.

Never modify src/: the matching Nintendo DS translation units must retain their
ROM ABI and source spellings. Every projection below is scoped to one source
file and has strict replacement counts so changed upstream code cannot silently
inherit an obsolete ARM64 fix. These projections are syntax/ABI migration steps:
successfully compiling an object does not establish link or runtime correctness.
"""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[2]


def exact(text, before, after, count=1):
    found = text.count(before)
    if found != count:
        raise ValueError("expected %d occurrence(s) of %r, found %d" %
                         (count, before, found))
    return text.replace(before, after)


def end_kuppa_script(text):
    # The scalar v holds the guest address of the current cutscene script,
    # not a native pointer. The two address-named data symbols refer to
    # EU cartridge addresses. Comparing v against a truncated host pointer
    # both fails on ARM64 and tests a different address space.
    text = exact(text, "v == (int)&data_020890a0", "v == 0x020890a0")
    text = exact(text, "v == (int)&data_02088fb8", "v == 0x02088fb8")
    return text


def stage_methods(text):
    # MWCC register hints are not C++17 syntax; no storage or value changes.
    for declaration in ("register u8 var_sl;", "register u8 var_fp;",
                        "register u8 *de8_sb;", "register u8 aa =",
                        "register u8 slot;", "register u8 tx;"):
        text = exact(text, declaration, declaration.replace("register ", "", 1))
    # This expression is explicitly discarded; preserve its sole effect (none)
    # without narrowing a live native address to the DS's u32 size.
    for symbol in ("data_0209f238", "gTouchHeld", "data_0209f2c8"):
        text = exact(text, "(void)(u32)&" + symbol + ";", "(void)&" + symbol + ";")
    return text


def crate_actor(text):
    # C++17 forbids jumping past a local initializer. Both flags are pure
    # masks of the same immutable value; evaluating B before the first goto
    # has no externally visible effect and keeps the two branches identical.
    text = exact(text,
        "    int inYoshiMouthA = (flags & 0x20000) ? 1 : 0;\n"
        "    if (inYoshiMouthA != 0) goto done;\n"
        "    int inYoshiMouthB = (flags & 0x40000) ? 1 : 0;",
        "    int inYoshiMouthA = (flags & 0x20000) ? 1 : 0;\n"
        "    int inYoshiMouthB = (flags & 0x40000) ? 1 : 0;\n"
        "    if (inYoshiMouthA != 0) goto done;")
    return text


PROJECTIONS = {
    "src/game/actors/d_a_btt_bk.cpp": crate_actor,
    "src/EndKuppaScript.cpp": end_kuppa_script,
    "src/game/stages/d_s_stage_methods.cpp": stage_methods,
}


def generate(out):
    result = {}
    for rel, transform in PROJECTIONS.items():
        source = ROOT / rel
        transformed = transform(source.read_text(encoding="utf-8"))
        dest = out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text("// Android-only projected TU; source: " + rel +
                        "\n// Matching DS source remains untouched.\n" +
                        transformed, encoding="utf-8")
        result[rel] = dest.resolve()
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args()
    for source, dest in generate(args.out.resolve()).items():
        print("%s -> %s" % (source, dest))

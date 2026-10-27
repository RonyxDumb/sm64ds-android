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
    # Source address of each host-owned object is a full native pointer.
    # The middle (int) cast was a MWCC register-allocation spelling only.
    text = exact(text,
        "*(MinimapDesc *)(((long long)(int)&data_0209f3c8)) = *desc;",
        "*reinterpret_cast<MinimapDesc *>(&data_0209f3c8) = *desc;")
    text = exact(text,
        "int *position = (int *)((long long)(int)&player->mPosX);",
        "int *position = reinterpret_cast<int *>(&player->mPosX);")
    # These symbols were linker-defined constants on DS; the host must pass
    # the overlay IDs rather than the host address of placeholder symbols.
    for number in (60, 98, 102):
        text = exact(text, "fn((int)&OVERLAY_%d_ID);" % number,
                     "fn(%d);" % number)
    # The original 32-bit scalar is a DS-style handle, NOT a place to
    # truncate a native C++ pointer. Encode the native fader pointer in
    # the existing checked packed::Space address table (used by frontend).
    text = exact(text, "data_0209d4b0 = (int)o2;",
                 "data_0209d4b0 = static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(o2)));",
                 2)
    text = exact(text, "data_0209d4b0 = (int)entry;",
                 "data_0209d4b0 = static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(entry)));")
    text = exact(text, "data_0209d4b0 == (int)((dFdWipe_c *)data_0209f324 + 6)",
                 "data_0209d4b0 == static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>((dFdWipe_c *)data_0209f324 + 6)))")
    # Stage globals which carry DS addresses still store checked 32-bit
    # handles; never persist a truncated native Player or texture pointer.
    text = exact(text, "func_ov002_020b18f0((int)data_0209f394[idx]);",
                 "func_ov002_020b18f0(static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(data_0209f394[idx]))));")
    text = exact(text, "data_0209f314 = (int)scene->mTexAnimSlots;",
                 "data_0209f314 = static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(scene->mTexAnimSlots)));")
    text = exact(text, "data_0209f320 = (int)components;",
                 "data_0209f320 = static_cast<int>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(components)));")
    text = exact(text, "data_0209f344 = (s32)&VS_STAR_SPAWN_ORDERS[idx];",
                 "data_0209f344 = static_cast<s32>(sm64ds::packed::encode(reinterpret_cast<std::uintptr_t>(&VS_STAR_SPAWN_ORDERS[idx])));")
    # The 64-bit AND ~0 expression is an identity operation on the address.
    # Read the original signed byte directly rather than truncating the host pointer.
    text = exact(text,
        "*(s8 *)(unsigned)(((unsigned long long)(unsigned)&data_0209f2f8) & ~0ULL)",
        "*reinterpret_cast<s8 *>(&data_0209f2f8)")
    return '#include "runtime/packed_ptr.h"\n' + text


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



def cap_icon(text):
    # The source extracts the low byte of a real member. This is an address
    # round-trip used only to steer 32-bit MWCC register allocation. Keep the
    # native member's full address and its original byte write.
    return exact(text,
                 "unsigned char *p1b = (unsigned char *)(int)&mFlags;",
                 "unsigned char *p1b = reinterpret_cast<unsigned char *>(&mFlags);")


PROJECTIONS = {
    "src/ui/hud/d_cap_icon.cpp": cap_icon,
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

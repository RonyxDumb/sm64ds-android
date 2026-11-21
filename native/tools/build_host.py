#!/usr/bin/env python3
"""Build the headless Linux test program (32-bit x86, GCC): the game, the platform layer, the host runner.

  1. tools/portify.py prepares the decomp's sources;
  2. every game unit is compiled, and its sections renamed to .m_<module>.* (tools/modgen.py groups them);
  3. tools/gapgen.py and tools/modgen.py write the symbols, relocation tables and module tables of this build;
  4. everything is linked into OUT/sm64ds.

Incremental (ninja). Usage: build_host.py <out dir> [--cc "gcc -m32"]
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "port"
sys.path.insert(0, str(ROOT / "tools"))
from game_flags import game_flags  # noqa: E402

PLATFORM = ["nullmem.c", "os_sm64ds.c", "mi_sm64ds.c", "card_sm64ds.c", "services_sm64ds.c", "runtime_sm64ds.c", "image.c",
            "hw.c", "frame.c", "gx3d.c", "gpu2d.c", "tex.c", "soft3d.c",
            "hostside/memory.c", "hostside/direct_sm64ds.c", "hostside/hostapi_impl.c", "hostside/online.c"]
HOST = ["host/main.c", "host/render.c"]


def modules(decomp):
    cfg = decomp / "config" / "arm9"
    mods = ["main"] + [m for m in ("itcm", "dtcm") if (cfg / m / "delinks.txt").exists()]
    return mods + sorted(d.name for d in (cfg / "overlays").iterdir())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--cc", default="gcc -m32")
    ap.add_argument("--cross", default="", help="cross toolchain prefix, e.g. arm-linux-gnueabihf- (then a static "
                    "program, run with qemu-arm on a PC)")
    args = ap.parse_args()
    out = Path(args.out).resolve()
    gen = out / "gen"
    decomp = ROOT / "decomp"
    out.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "portify.py"), str(decomp), str(gen),
                           str(PORT / "exclude.txt")])
    mods = modules(decomp)
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "modgen.py"), str(gen / "mod")] + mods)
    cross = args.cross
    cc = f"{cross}gcc-13" if cross else args.cc
    objcopy, nm = f"{cross}objcopy", f"{cross}nm"
    gcc = "gcc" in cc
    # 32-bit ARM: the game in ARM state (port/platform/nullmem.c decodes A32 only).
    target_flags = ["-marm"] if cross.startswith("arm-") else []
    lines = [
        f"cc = {cc}",
        "rule game",
        "  command = $cc $flags -MD -MF $out.d -c $in -o $out.raw && "
        f"{objcopy} --prefix-alloc-sections=.m_$module --redefine-syms={PORT / 'game_symbol_renames.txt'} $out.raw $out && "
        "rm -f $out.raw",
        "  depfile = $out.d",
        "  deps = gcc",
        "  description = game $in",
        "rule cc",
        "  command = $cc $flags -MD -MF $out.d -c $in -o $out",
        "  depfile = $out.d",
        "  deps = gcc",
        "  description = cc $in",
        "rule link",
        f"  command = $cc -no-pie {'-static' if cross else ''} -o $out @$out.rsp {out / 'gen' / 'symbols.ld'} {PORT / 'symbols_extra.ld'} "
        f"-Wl,-T,{out / 'gen' / 'sections.ld'} -lpthread -lm -lgcc -Wl,--no-wchar-size-warning",
        "  rspfile = $out.rsp",
        "  rspfile_content = $in",
        "  description = link $out",
    ]
    game_objs, plat_objs = [], []
    for line in (gen / "units.txt").read_text().splitlines():
        module, rel = line.split(" ", 1)
        src = gen / "src" / rel
        obj = out / "obj" / "game" / (rel + ".o")
        flags = game_flags(gen, rel, gcc=gcc) + ["-fuse-cxa-atexit"] + target_flags
        lines += [f"build {obj}: game {src}", f"  flags = {' '.join(f for f in flags if f != '-fno-use-cxa-atexit')}",
                  f"  module = {module}"]
        game_objs.append(obj)
    for src in sorted((PORT / "game").glob("*.cpp")):
        obj = out / "obj" / "port_game" / (src.name + ".o")
        flags = [f for f in game_flags(gen, "x.cpp", gcc=gcc) if f != "-fno-use-cxa-atexit"] + ["-fuse-cxa-atexit"] + target_flags
        lines += [f"build {obj}: game {src}", f"  flags = {' '.join(flags)}", "  module = main"]
        game_objs.append(obj)
    inc = ["-I", str(gen / "include"), "-I", str(gen / "include" / "nitro"), "-I", str(PORT / "platform" / "include"),
           "-I", str(PORT / "platform"), "-I", str(PORT / "guest"), "-I", str(out / "gen" / "mod")]
    pflags = ["-O2", "-g", "-std=gnu11", "-fno-strict-aliasing", "-fwrapv", "-fno-pie", "-fsigned-char", "-w",
              "-DSM64DS_PLATFORM=1"] + inc
    for name in PLATFORM + HOST:
        src = PORT / ("platform" if not name.startswith("host/") else "") / name if not name.startswith("host/") else PORT / name
        obj = out / "obj" / "platform" / (name.replace("/", "_") + ".o")
        lines += [f"build {obj}: cc {src}", f"  flags = {' '.join(pflags)}"]
        plat_objs.append(obj)
    lines.append(f"build objects: phony {' '.join(str(o) for o in game_objs + plat_objs)}")
    gen_objs = []
    for name in ["gap/gap_relocs.S", "mod/modules.c"]:
        src = out / "gen" / name
        obj = out / "obj" / "gen" / (Path(name).name + ".o")
        lines += [f"build {obj}: cc {src}", f"  flags = {' '.join(pflags)}"]
        gen_objs.append(obj)
    exe = out / "sm64ds"
    lines.append(f"build {exe}: link {' '.join(str(o) for o in game_objs + plat_objs + gen_objs)} | "
                 f"{out / 'gen' / 'symbols.ld'} {out / 'gen' / 'sections.ld'} {PORT / 'symbols_extra.ld'}")
    lines.append(f"default {exe}")
    (out / "build.ninja").write_text("\n".join(lines) + "\n")
    verbose = ["-v"] if os.environ.get("VERBOSE") else []
    subprocess.check_call(["ninja", "-C", str(out), "objects"] + verbose)
    # The symbols every object defines, then the generated symbols and tables.
    defined = set()
    for o in game_objs + plat_objs:
        r = subprocess.run([nm, "-g", "--defined-only", str(o)], capture_output=True, text=True, check=True)
        for ln in r.stdout.splitlines():
            parts = ln.split()
            if len(parts) == 3:
                defined.add(parts[2])
    for ln in (PORT / "symbols_extra.ld").read_text().splitlines():
        if ln.startswith("PROVIDE("):
            defined.add(ln[len("PROVIDE("):].split("=")[0].strip().strip('"'))
    (out / "gen" / "defined.txt").write_text("\n".join(sorted(defined)) + "\n")
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "gapgen.py"), str(decomp), str(out / "gen" / "gap"),
                           "--defined", str(out / "gen" / "defined.txt"), "--exclude", str(PORT / "exclude.txt")])
    (out / "gen" / "symbols.ld").write_text((out / "gen" / "gap" / "symbols.ld").read_text())
    (out / "gen" / "sections.ld").write_text((out / "gen" / "mod" / "sections.ld").read_text())
    subprocess.check_call(["ninja", "-C", str(out)] + verbose)


if __name__ == "__main__":
    main()

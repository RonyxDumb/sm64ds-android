#!/usr/bin/env python3
"""Compile every prepared decomp unit (tools/portify.py output) with the port's game flags and report failures.

Usage: compile_census.py <gen dir> <out dir> [compiler driver, default "clang -m32"]
Writes OUT/failed.txt (one unit per line) and keeps each failure's log at OUT/log/<unit>.txt.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from game_flags import game_flags  # noqa: E402


def main():
    gen, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    cc = sys.argv[3] if len(sys.argv) > 3 else "clang -m32"
    out.mkdir(parents=True, exist_ok=True)
    lines = ["rule cc", "  command = sh -c '$cc $flags -c $in -o $out 2> $log || { cat $log; exit 1; }'",
             "  description = $in"]
    for src in (gen / "sources.txt").read_text().split():
        rel = Path(src).relative_to(gen / "src").as_posix()
        obj = out / "obj" / (rel + ".o")
        log = out / "log" / (rel.replace("/", "_") + ".txt")
        log.parent.mkdir(parents=True, exist_ok=True)
        lines += [f"build {obj}: cc {src}", f"  cc = {cc}", f"  flags = {' '.join(game_flags(gen, rel, gcc='gcc' in cc or 'g++' in cc))}",
                  f"  log = {log}"]
    (out / "build.ninja").write_text("\n".join(lines) + "\n")
    subprocess.run(["ninja", "-C", str(out), "-k", "0"], stdout=subprocess.DEVNULL)
    failed = []
    for src in (gen / "sources.txt").read_text().split():
        rel = Path(src).relative_to(gen / "src").as_posix()
        if not (out / "obj" / (rel + ".o")).exists():
            failed.append(rel)
    (out / "failed.txt").write_text("".join(f + "\n" for f in failed))
    total = len((gen / "sources.txt").read_text().split())
    print(f"census: {total - len(failed)}/{total} compiled, {len(failed)} failed")


if __name__ == "__main__":
    main()

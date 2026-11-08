#!/usr/bin/env python3
"""Compile the generated frontend/stage/minigame units with clang and the
APK's own warning flags (-fsyntax-only), so NDK-only diagnostics such as
-Wunused-local-typedef are caught before CI.

Flags are read from android/app/build.gradle (cppFlags) and from the
sm64ds_frontend target_compile_options in android/app/src/main/cpp/
CMakeLists.txt, so this check follows the real build configuration.

Usage: ndk_warning_check.py --generated DIR [--generated DIR ...] --include DIR ...
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def build_flags():
    gradle = (ROOT/'android/app/build.gradle').read_text()
    match = re.search(r"cppFlags\s+'([^']*)'", gradle)
    flags = shlex.split(match[1]) if match else []
    cmake = (ROOT/'android/app/src/main/cpp/CMakeLists.txt').read_text()
    for block in re.findall(r'target_compile_options\(\s*sm64ds_frontend\s+PRIVATE\s+([^)]*)\)', cmake):
        flags += [f for f in shlex.split(block) if f.startswith(('-W', '-f'))]
    return flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, action='append', required=True)
    parser.add_argument('--include', type=Path, action='append', default=[])
    parser.add_argument('--compiler', default=shutil.which('clang++') or 'clang++')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1))
    args = parser.parse_args()
    # Include order of the sm64ds_frontend target: projected headers first.
    flags = [args.compiler] + build_flags() + ['-fsyntax-only']
    flags += ['-I'+str(p) for p in args.include]
    flags += ['-I'+str(ROOT/'android/app/src/main/cpp'), '-I'+str(ROOT/'port/ntr/include'), '-I'+str(ROOT/'include')]
    sources = sorted(p for d in args.generated for p in d.glob('*.cpp'))

    def check(source):
        result = subprocess.run(flags+[str(source)],
                                capture_output=True, text=True)
        return source, result
    failed = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for source, result in pool.map(check, sources):
            if result.returncode:
                failed += 1
                print(source.name+':\n'+'\n'.join(l for l in result.stderr.splitlines() if 'error' in l)[:2000])
    print('ndk warning check: %d/%d units clean' % (len(sources)-failed, len(sources)))
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()

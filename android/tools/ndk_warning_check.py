#!/usr/bin/env python3
"""Compile the generated frontend/stage/minigame units with clang and the
APK's own warning flags (-fsyntax-only), so NDK-only diagnostics such as
-Wunused-local-typedef are caught before CI.

Flags are read from android/app/build.gradle (cppFlags) and from the
sm64ds_frontend target_compile_options in android/app/src/main/cpp/
CMakeLists.txt, so this check follows the real build configuration.

With --native GENERATED (the native_sources.py output, NATIVE_ROOT in
CMake), the hand-written runtime/*.cpp sources of the sm64ds_android target
are checked too, with that target's definitions, options and per-source
COMPILE_OPTIONS.

Usage: ndk_warning_check.py [--generated DIR ...] [--include DIR ...] [--native DIR]
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


def native_sources(cmake):
    """runtime/*.cpp of the sm64ds_android target, with per-source options."""
    names = []
    for block in re.findall(r'(?:add_library|target_sources)\(\s*sm64ds_android\b(.*?)\)\n', cmake, re.S):
        names += [n for n in re.findall(r'(?<![/\w])runtime/\w+\.cpp', block) if n not in names]
    options = {}
    for files, opts in re.findall(r'set_source_files_properties\(([^)]*?)\s+PROPERTIES COMPILE_OPTIONS "([^"]*)"', cmake):
        for name in re.findall(r'(?<![/\w])runtime/\w+\.cpp', files):
            options[name] = opts.split(';')
    return [(n, options.get(n, [])) for n in names]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, action='append', default=[])
    parser.add_argument('--native', type=Path, help='native_sources.py output (CMake NATIVE_ROOT)')
    parser.add_argument('--include', type=Path, action='append', default=[])
    parser.add_argument('--compiler', default=shutil.which('clang++') or 'clang++')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1))
    args = parser.parse_args()
    # Include order of the sm64ds_frontend target: projected headers first.
    flags = [args.compiler] + build_flags() + ['-fsyntax-only']
    flags += ['-I'+str(p) for p in args.include]
    flags += ['-I'+str(ROOT/'android/app/src/main/cpp'), '-I'+str(ROOT/'port/ntr/include'), '-I'+str(ROOT/'include')]
    jobs = [(p, flags) for p in sorted(p for d in args.generated for p in d.glob('*.cpp'))]
    if args.native:
        cpp = ROOT/'android/app/src/main/cpp'
        cmake = (cpp/'CMakeLists.txt').read_text()
        native = [args.compiler]
        gradle = (ROOT/'android/app/build.gradle').read_text()
        match = re.search(r"cppFlags\s+'([^']*)'", gradle)
        native += (shlex.split(match[1]) if match else []) + ['-fsyntax-only', '-DSM64DS_PLATFORM_PC']
        native += ['-fvisibility=hidden', '-ffunction-sections', '-fdata-sections', '-fno-strict-aliasing']
        # A host check cannot define __ANDROID__ (no NDK sysroot), so members
        # only used under it (game_program's phone-only traces) look unused.
        native += ['-Wno-unused-private-field']
        native += ['-I'+str(ROOT/'port/ntr/include'), '-I'+str(ROOT/'include'),
                   '-I'+str(ROOT/'external/sm64ds-pc/port/hal/sdat'), '-I'+str(cpp), '-I'+str(args.native)]
        jobs += [(cpp/name, native+opts) for name, opts in native_sources(cmake)]
    sources = [p for p, _ in jobs]

    def check(job):
        source, command = job
        result = subprocess.run(command+[str(source)],
                                capture_output=True, text=True)
        return source, result
    failed = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for source, result in pool.map(check, jobs):
            if result.returncode:
                failed += 1
                print(source.name+':\n'+'\n'.join(l for l in result.stderr.splitlines() if 'error' in l)[:2000])
    print('ndk warning check: %d/%d units clean' % (len(sources)-failed, len(sources)))
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Compile the generated native frontend on a host with bounded parallelism."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--jobs',type=int,default=min(4,os.cpu_count() or 1))
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    flags=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-unknown-pragmas',
           '-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare',
           '-Wno-comment','-Wno-implicit-fallthrough','-Wno-misleading-indentation',
           '-Wno-unused-but-set-variable','-Wno-unused-value','-fwrapv','-fsigned-char',
           '-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
           '-fsanitize=address,undefined','-g','-Iandroid/app/src/main/cpp',
           '-Iport/ntr/include','-I'+str(args.generated/'include'),'-Iinclude']
    def compile_one(source):
        result=subprocess.run(flags+['-c',str(source),'-o',str(args.out/(source.name+'.o'))],
                              capture_output=True,text=True)
        return source,result
    with ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        results=list(pool.map(compile_one,sorted(args.generated.glob('*.cpp'))))
    for source,result in results:
        if result.returncode:
            print(str(source)+':\n'+result.stdout+result.stderr)
    raise SystemExit(any(result.returncode for _,result in results))


if __name__=='__main__':
    main()

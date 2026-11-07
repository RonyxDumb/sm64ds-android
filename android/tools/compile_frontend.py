#!/usr/bin/env python3
"""Compile the generated native frontend on a host with bounded parallelism."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--jobs',type=int,default=min(4,os.cpu_count() or 1))
    parser.add_argument('--incremental',action='store_true',
                        help='Reuse objects only when source, headers and compiler options match.')
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    flags=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-unknown-pragmas',
           '-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare',
           '-Wno-comment','-Wno-implicit-fallthrough','-Wno-misleading-indentation',
           '-Wno-unused-but-set-variable','-Wno-unused-local-typedefs','-Wno-unused-value','-fwrapv','-fsigned-char',
           '-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
           '-fsanitize=address,undefined','-g','-Iandroid/app/src/main/cpp',
           '-Iport/ntr/include','-I'+str(args.generated/'include'),'-Iinclude']
    # Content checks also catch generated files rewritten with older timestamps.
    # Any project/generated header edit invalidates the cache, including inline
    # functions in the packed-pointer and hardware-register implementations.
    common=hashlib.sha256(json.dumps(flags).encode())
    if args.incremental:
        compiler=Path(shutil.which(flags[0])).resolve()
        common.update(compiler.read_bytes())
        common.update(subprocess.check_output([flags[0],'-dumpmachine']))
        for root in (Path('include'),Path('port/ntr/include'),
                     Path('android/app/src/main/cpp'),args.generated/'include'):
            for header in sorted(p for p in root.rglob('*') if p.suffix in ('.h','.hpp','.inc')):
                common.update(str(header).encode())
                common.update(header.read_bytes())
    def compile_one(source):
        obj=args.out/(source.name+'.o')
        stamp=args.out/(source.name+'.sha256')
        fingerprint=common.copy()
        fingerprint.update(source.read_bytes())
        digest=fingerprint.hexdigest()
        if args.incremental and obj.exists() and stamp.exists() and stamp.read_text()==digest:
            return source,subprocess.CompletedProcess([],0,'','')
        result=subprocess.run(flags+['-c',str(source),'-o',str(obj)],
                              capture_output=True,text=True)
        if result.returncode==0:
            stamp.write_text(digest)
        else:
            stamp.unlink(missing_ok=True)
        return source,result
    sources=sorted(args.generated.glob('*.cpp'))
    expected={source.name+'.o' for source in sources}
    # A removed projection must never survive as a stale linked object.
    for obj in args.out.glob('*.cpp.o'):
        if obj.name not in expected:
            obj.unlink()
            obj.with_suffix('.sha256').unlink(missing_ok=True)
    with ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        results=list(pool.map(compile_one,sources))
    for source,result in results:
        if result.returncode:
            print(str(source)+':\n'+result.stdout+result.stderr)
    raise SystemExit(any(result.returncode for _,result in results))


if __name__=='__main__':
    main()

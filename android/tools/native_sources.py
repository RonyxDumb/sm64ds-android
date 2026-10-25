#!/usr/bin/env python3
"""Generate audited ARM64 variants without changing the matching source tree."""
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
import hostgen


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"source changed: expected exactly one {old!r}")
    return text.replace(old, new, 1)


def save_io(text):
    # Only the card write's source argument is a host pointer. Medium offsets,
    # record lengths and the original on-disk fields remain 32 bit.
    text = replace_once(text,
        "int func_02060484(int a, int b, int c, int d, int e, int f);",
        "int func_02060484(u32 a, const void* b, u32 c, int d, int e, int f);")
    for pointer in ("&sum", "data_020a4b40", "data"):
        expected = f"(int){pointer},"
        if text.count(expected) != 2:
            raise ValueError(f"card source changed: {expected!r}")
        text = text.replace(expected, f"{pointer},")
    # The recovered read path consults ok after an I/O failure. Its original
    # declaration leaves that path undefined in C++; a failed primary read is
    # not a valid primary record. The mirror fallback remains the original code.
    text = replace_once(text, "    int ok;", "    int ok = 0;")
    return text


def function(text, signature):
    """Select a whole recovered function, skipping strings/comments for braces."""
    if text.count(signature) != 1:
        raise ValueError(f"source changed: expected exactly one definition {signature!r}")
    begin = text.index(signature)
    brace = text.index('{', begin)
    depth = 0
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for token in tokens.finditer(text, brace):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return text[begin:token.end()]
    raise ValueError(f"unclosed function {signature}")


def shared_file(text):
    # The same seam selected by PC port/hal/fs.cpp. Keep the original typed
    # refcount/ownership logic; do not compile the DS card/DMA implementation
    # or the downstream fixed-layout model/collision mutation in this slice.
    signatures = (
        'extern "C" void func_02017e0c(void *self, unsigned int fileID) {',
        'extern "C" void *func_02017e48(void *self, unsigned int fileID) {',
        'extern "C" void func_02017c24(SharedFilePtr *self) {',
        'void *SharedFilePtr::LoadFile() {',
        'void SharedFilePtr::Release() {',
        'SharedFilePtr &SharedFilePtr::Construct(unsigned int fileID) {',
    )
    prefix = '''#include "SharedFilePtr.h"
namespace Memory { void Deallocate(void *); }
extern "C" { extern unsigned int data_0209d3bc; unsigned int func_02018a24(unsigned int); }
'''
    return prefix + '\n\n'.join(function(text, sig) for sig in signatures) + '\n'


def generate(out):
    sources = ("src/engine/save/SaveData.cpp", "src/engine/save/SaveData2.cpp")
    for rel in sources:
        source = ROOT / rel
        text = source.read_text()
        if rel.endswith("SaveData2.cpp"):
            text = save_io(text)
        text, _ = hostgen.transform(text)
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(f'// Generated from {rel}; edit android/tools/native_sources.py.\n' + text)
    rel = 'src/SharedFilePtr.cpp'
    target = out / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(f'// Generated from {rel}; exact recovered ownership functions.\n' +
                      shared_file((ROOT / rel).read_text()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    generate(parser.parse_args().out)

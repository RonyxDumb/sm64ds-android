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


def boot(text):
    # Project the seven scalar leaf fields onto native-owned state. The DS
    # actor manager, vtable and member-pointer layouts are not instantiated.
    signatures = ('s32 dScBoot_c::InitResources() {', 's32 dScBoot_c::Behavior() {',
                  'void dScBoot_c::func_02005348() {')
    state = re.search(r'enum BootMenuState \{.*?\n\};', text, re.S)
    if state is None:
        raise ValueError('boot state enumeration changed')
    output = '#include "runtime/boot_seams.h"\n' + state.group() + '\n'
    output += 'enum { ASSET_D_2D_FRAMEWORK_INITIALIZE_NSC_BIN = 0x022d };\n#define LADR(p) ((void *)(p))\n'
    for sig in signatures:
        method = function(text, sig).replace('dScBoot_c::', 'sm64ds::BootState::')
        if 's32 dScBoot_c::Behavior' in sig:
            method = replace_once(method, 'int palette;', 'void *palette;')
            method = method.replace('StartSceneFade(', 'dScene_c::StartSceneFade(')
        output += method + '\n\n'
    return hostgen.transform(output)[0]


def boot_header(text):
    start = text.index('    u16 mFadeTimer;')
    end = text.index('    /* Declared first, deliberately:', start)
    fields = text[start:end]
    expected = ('mFadeTimer', 'mState', 'mSelectedButton', 'mButtonFlashTimer',
                'mInputLockTimer', 'mEraseEffectTimer', 'pad_057')
    if tuple(re.findall(r'\b(?:u16|u8)\s+(\w+)', fields)) != expected:
        raise ValueError('boot leaf fields changed; audit native ownership')
    return ('#pragma once\n#include "types.h"\nnamespace sm64ds {\nstruct BootState {\n' + fields +
            '    s32 InitResources();\n    s32 Behavior();\n    void func_02005348();\n};\n}\n')


def faders(text):
    signatures = ('dFader_c::~dFader_c()', 'dFdBrightness_c::~dFdBrightness_c()',
        'void dFdBrightness_c::AdvanceFade()', 'int dFdBrightness_c::SetBackwardTime(u32 frames)',
        'int dFdBrightness_c::SetForwardTime(u32 frames)', 'int dFdBrightness_c::IsAtStart()',
        'int dFdBrightness_c::IsAtEnd()', 'int dFdBrightness_c::IsBetweenStartAndEnd()',
        'void dFdBrightness_c::SetToEnd()', 'void dFdBrightness_c::SetToStart()',
        'void dFader_c::AdvanceInterp()', 'dFdColor_c::~dFdColor_c()', 'void dFdColor_c::AdvanceFade()')
    output = '''#include "runtime/boot_seams.h"
int ApproachLinear(Fix12i &, Fix12i, Fix12i);
namespace cstd { Fix12i fdiv(Fix12i, Fix12i); }
extern "C" void func_02053a90(u16 *, int);
extern "C" void _ZN3G2x18SetBlendBrightnessEPVtts(volatile u16 *, u16, int);
'''
    for sig in signatures:
        method = function(text, sig)
        if sig == 'void dFdBrightness_c::AdvanceFade()':
            # Native CPU copies are coherent; there is no DS DMA/cache seam.
            method = replace_once(method, '    CP15::FlushAndInvalidateDataCache((u32)data_0209d3ac, 2);', '')
        output += method + '\n\n'
    return hostgen.transform(output)[0]


def scene_transitions(text):
    signatures = ('int dScene_c::SetSceneToSpawn(u32 sceneID, u32 param)',
        'void dScene_c::StartSceneFade(u32 sceneID, u32 param, u16 fadeColor)',
        'void dScene_c::SetFaders(dFdBrightness_c *fader)', 'void dScene_c::SetAndStopColorFader()')
    return '#include "runtime/boot_seams.h"\n' + '\n\n'.join(function(text, sig) for sig in signatures)


def font(text):
    # Only resident file pointers change; mapped VRAM addresses stay u32.
    pairs = (
        ('extern "C" int LoadFile(int handle);', 'extern "C" unsigned char *LoadFile(int handle);'),
        ('extern "C" int Mg_GetWorkBuffer(void);', 'extern "C" void *Mg_GetWorkBuffer(void);'),
        ('extern "C" int LoadFileAt(int handle, int dest);', 'extern "C" int LoadFileAt(int handle, void *dest);'),
        ('extern int data_0209d6f8;', 'extern void *data_0209d6f8;'),
        ('    int file;', '    unsigned char *file;'), ('    int r4;', '    unsigned char *r4;'),
    )
    for old, new in pairs:
        text = replace_once(text, old, new)
    return hostgen.transform(text)[0]


def message_parser(text):
    for name in ('gMessageBankBase', 'gMessageBankHeader', 'gMessageInfoSection',
                 'gMessageDataSection', 'gMessageEntries'):
        text = replace_once(text, f'extern int {name};', f'extern unsigned char *{name};')
    for old, new in (('    int bankBase =', '    unsigned char *bankBase ='),
                     ('    int infoSection =', '    unsigned char *infoSection ='),
                     ('    int dataSection =', '    unsigned char *dataSection =')):
        text = replace_once(text, old, new)
    return text


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
    generated = {
        'runtime/boot_state.h': boot_header((ROOT / 'include/dScBoot_c.h').read_text()),
        'runtime/boot.cpp': boot((ROOT / 'src/game/stages/d_s_boot_methods.cpp').read_text()),
        'runtime/faders.cpp': faders((ROOT / 'src/runtime/graphics/fader/d_fader.cpp').read_text()),
        'runtime/transitions.cpp': scene_transitions((ROOT / 'src/game/stages/d_scene.cpp').read_text()),
        'runtime/font.cpp': font((ROOT / 'src/LoadFont.cpp').read_text()),
        'runtime/bg.cpp': '#include "ntr/mmio.h"\n' + hostgen.transform((ROOT / 'src/engine/gx/G2.cpp').read_text())[0],
        'runtime/approach.cpp': function((ROOT / 'src/ApproachAngle.cpp').read_text(), 'int ApproachLinear(int &ref, int target, int step)'),
        'runtime/message_parser.cpp': 'extern "C" {\n' + message_parser((ROOT / 'src/ui/messages/func_0201cb2c.c').read_text()) + '\n}\n',
    }
    for rel, text in generated.items():
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('// Generated from recovered source by android/tools/native_sources.py.\n' + text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    generate(parser.parse_args().out)

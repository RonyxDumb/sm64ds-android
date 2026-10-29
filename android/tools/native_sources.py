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
        'int dScene_c::SpawnIfNecessary()',
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


def title_records(text):
    record = re.search(r'struct FileSaveData \{.*?\n\};', text, re.S)
    if record is None:
        raise ValueError('title file record changed')
    return ('#pragma once\n#include "types.h"\n' + record.group() +
            '\nstatic_assert(sizeof(FileSaveData) == 0x44, "cartridge file record");\n')


def title_selection(text):
    # This is the actual file-launch branch of dScDSMT_c::Behavior. Its UI
    # dispatcher is a separate dependency; do not replace it with guessed
    # button coordinates or claim that the whole title scene is running.
    behavior = function(text, 's32 dScDSMT_c::Behavior() {')
    branch = function(behavior, 'if ((unsigned int)(result - 3) <= 2) {')
    branch = replace_once(branch, '(unsigned int)(result - 3)',
                          '(static_cast<unsigned int>(result) - 3u)')
    branch = replace_once(branch, 'data_0209caa0);',
                          'reinterpret_cast<FileSaveData *>(data_0209caa0));')
    branch = branch[:-1] + '    return true;\n}'
    return ('#include "runtime/title_files.h"\n#include "runtime/stage_flow.h"\n'
            'bool sm64ds::TitleFiles::start_selected(int result) {\n' + branch +
            '\nreturn false;\n}\n')


def title_files(text):
    output = ('#include "runtime/title_files.h"\n#include "runtime/stage_flow.h"\n'
              '#include "SaveData.h"\n')
    for signature in (
        'extern "C" FileSaveData *func_ov007_020cc0cc(int idx) {',
        'extern "C" void func_ov007_020cc0e4(FileSaveData *files) {',
        'extern "C" void func_ov007_020cc168(u32 saveSlot) {',
    ):
        method = function(text, signature)
        if 'void func_ov007_020cc168' in signature:
            method = replace_once(method, 'data_0208ee3c[0]', 'data_0208ee3c')
        output += method + '\n'
    command = function(text, 'extern "C" u8 func_ov007_020cc600(s32 command) {')
    load = function(command, 'if (command == 20) {')
    body = load[load.index('{') + 1:-1]
    body = replace_once(body, '(u8 *)_ZN6Memory13operator_new2Ej(0xcc)',
                         'reinterpret_cast<u8 *>(files_.data())')
    body = replace_once(body,
        'SaveData::ReadFileData(saveSlot, (FileSaveData *)fileBytes) == 0',
        '!(readOk_[saveSlot] = SaveData::ReadFileData(saveSlot, (FileSaveData *)fileBytes) != 0)')
    body = replace_once(body, 'func_02013c84(saveSlot, fileBytes, -1, data_0209caa0);',
        'func_02013c84(saveSlot, reinterpret_cast<FileSaveData *>(fileBytes), -1, '
        'reinterpret_cast<FileSaveData *>(data_0209caa0));')
    output += 'bool sm64ds::TitleFiles::load() {\nu8 failed = 0;\n' + body + '\nreturn failed == 0;\n}\n'
    command = replace_once(command, 'extern "C" u8 func_ov007_020cc600(s32 command)',
                            'u8 sm64ds::TitleFiles::command(s32 command)')
    command = replace_once(command,load,'if (command == 20) { failed = load() ? 0 : 1; }')
    command = command.replace('data_0209caa0);','reinterpret_cast<FileSaveData *>(data_0209caa0));')
    output += ('extern "C" { void SetSoundMode(int); void TurnBacklightOn(); void TurnBacklightOff();\n'
               'int func_ov007_020cc118(int,unsigned); extern u8 data_0209cae4[]; }\n'
               'namespace Sound { void StopLoadedMusic_Layer1(unsigned); }\n'+command+'\n')
    return output


def stage_flow(text):
    signatures = (
        'extern "C" void SetNextStar(void) {',
        'extern "C" void InitControllerMode(int idx, u8 val) {',
        'extern "C" void SetControllerMode(unsigned char mode) {',
        'extern "C" void SetPlayerGlobals(void) {',
        'extern "C" void LoadLevel(int levelID, u32 entranceID, int starID, u32 arg3, s8 returnState) {',
        'extern "C" void LoadLevelNoReturn(int levelID, u32 arg1, u32 arg2, u32 arg3) {',
        'extern "C" void StartFile(s8 levelID, u8 entranceID) {',
    )
    output = '#include "runtime/stage_flow.h"\n#include "runtime/boot_seams.h"\n'
    for signature in signatures:
        method = function(text, signature)
        # The active save is an actual 0x32c-byte record owned by SaveStore.
        # This TU views it in bytes; SaveData.cpp views the same storage in words.
        if any(name in signature for name in ('void InitControllerMode(',
                                               'void SetControllerMode(',
                                               'void SetPlayerGlobals(')):
            method = replace_once(method, 'data_0209caa0[0x42]',
                                   'reinterpret_cast<u8 *>(data_0209caa0)[0x42]')
        if 'void StartFile(' in signature:
            method = replace_once(method, 'data_0209caa0[0x41]',
                                   'reinterpret_cast<u8 *>(data_0209caa0)[0x41]')
        if 'data_0209caa0[' in method:
            raise ValueError('new active-save view in stage entry; audit byte/word units')
        if 'void LoadLevel(' in signature:
            # dFdColor_c is a genuine native C++ object: its vptr is eight
            # bytes, so the DS +0xc field access must name the actual member.
            method = replace_once(method,
                '*(unsigned short *)((char *)&data_0209f5e8 + 0xc) = 0;',
                'data_0209f5e8.color = 0;')
        output += method + '\n\n'
    return output


def controller_header(text):
    record = re.search(r'struct CamInput \{.*?\n\};', text, re.S)
    if record is None:
        raise ValueError('stage controller record changed')
    fields = re.findall(r'^\s*(s16|u16|u8)\s+(\w+);', record.group(), re.M)
    expected = [('s16', name) for name in ('dx', 'dy')]
    expected += [('u16', name) for name in ('held', 'pressed')]
    expected += [('s16', name) for name in ('mag', 'nx', 'ny', 'ang')]
    expected += [('u8', name) for name in ('tx', 'ty', 'delay', 'cnt', 'touching', 'pad15', 'mode', 'pad17')]
    if fields != expected:
        raise ValueError('stage controller fields changed; audit native ownership')
    return ('#pragma once\n#include "types.h"\n#include <cstddef>\nnamespace sm64ds {\n' +
            record.group().replace('struct CamInput', 'struct ControllerInput') +
            '\nstatic_assert(sizeof(ControllerInput) == 0x18);\n'
            'static_assert(offsetof(ControllerInput, mode) == 0x16);\n}\n')


def stage_input(text):
    output = ('#include "runtime/stage_flow.h"\n#include "runtime/boot_seams.h"\n'
              '#include "nitro/fx/fx.h"\nusing CamInput = sm64ds::ControllerInput;\n'
              'struct TouchInfo { u8 held, edge, x, y; };\n'
              '#define TOUCH_DATA ((TouchInfo *)gTouchHeld)\n'
              'namespace cstd { int sqrt(unsigned long long); s16 atan2(s32, s32); '
              'Fix12i fdiv(Fix12i, Fix12i); }\n')
    reset = function(text, 'extern "C" void ResetInput(void) {')
    reset = replace_once(reset, 'extern "C" void ResetInput(void)',
                         'void sm64ds::StageFlow::reset_input()')
    output += reset + '\n'
    camera = function(text, 'void dScStage_c::CheckCameraInput() {')
    camera = replace_once(camera, 'void dScStage_c::CheckCameraInput()',
                          'void sm64ds::StageFlow::check_camera_input(u32 cameraFlags)')
    # The recovered camera-input logic only reads this one field from the
    # camera. Its owner passes the actual flags, not a fake DS camera object.
    camera = replace_once(camera, '((dCamera_c *)data_0209f318)->mFlags', 'cameraFlags')
    output += camera + '\n'
    controls = function(text, 'void dScStage_c::CheckInput() {')
    controls = replace_once(controls, 'void dScStage_c::CheckInput()',
                            'void sm64ds::StageFlow::check_input()')
    controls = replace_once(controls, '    u8 mode = data_0209f2d8;',
                            '    inputEnabled_ = false;\n    u8 mode = data_0209f2d8;')
    controls = replace_once(controls, '    }\n    CamInput *control = (CamInput *)data_0209f498;',
                            '    }\n    inputEnabled_ = true;\n'
                            '    CamInput *control = (CamInput *)data_0209f498;')
    for old, new, count in (
        ('data_020a0e58[playerIndex].held', 'data_020a0e58[playerIndex * 2]', 2),
        ('data_020a0e58[playerIndex].pressed', 'data_020a0e58[playerIndex * 2 + 1]', 1),
        # DS shifts negative signed deltas; multiplication has the same result
        # in these bounded Q12 coordinates without native C++ shift UB.
        ('(deltaX << 12)', '(deltaX * 4096)', 1),
        ('(deltaY << 12)', '(deltaY * 4096)', 1),
    ):
        if controls.count(old) != count:
            raise ValueError(f'stage input changed: {old!r}')
        controls = controls.replace(old, new)
    return output + controls + '\n'


def input_atan2(text):
    method = function(text, 's16 atan2(s32 y, s32 x) {')
    # ITCM 01ffa4bc converts the integer to a float and 01ff98f4 tests <.
    # The old int prototypes describe the soft-float register ABI, not native
    # AArch64 FP delivery. Conversion to finite float preserves an int's sign.
    method = replace_once(method, 'func_01ff98f4(func_01ffa4bc(x), 0)', 'x < 0')
    return ('#include "runtime/stage_flow.h"\nnamespace cstd {\n'
            's32 fdiv(s32, s32);\n' +
            method.replace('_ZN4cstd4fdivEii(', 'fdiv(') + '\n}\n')


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
        'runtime/title_records.h': title_records((ROOT / 'src/game/stages/d_s_dsmt.cpp').read_text()),
        'runtime/title_selection.cpp': title_selection((ROOT / 'src/game/stages/d_s_dsmt.cpp').read_text()),
        'runtime/title_files.cpp': title_files((ROOT / 'src/game/stages/d_s_dsmt.cpp').read_text()),
        'runtime/stage_flow.cpp': stage_flow((ROOT / 'src/game/stages/d_s_stage_methods.cpp').read_text()),
        'runtime/stage_input.cpp': stage_input((ROOT / 'src/game/stages/d_s_stage_methods.cpp').read_text()),
        'runtime/controller_input.h': controller_header((ROOT / 'src/game/stages/d_s_stage_methods.cpp').read_text()),
        'runtime/input_atan2.cpp': input_atan2((ROOT / 'src/cstd_atan2.cpp').read_text()),
    }
    for rel, text in generated.items():
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('// Generated from recovered source by android/tools/native_sources.py.\n' + text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    generate(parser.parse_args().out)

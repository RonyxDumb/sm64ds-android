#!/usr/bin/env python3
"""Regenerate complete stage lifecycle bodies with checked cartridge records.

Builds consume the committed manifest using the stdlib-only packed_sources.py.
Regeneration uses the same tree-sitter packages as the full source census.
Every imported function is a typed checked native-code lookup, so a missing
callee fails at its actual call rather than becoming a linker stub.
"""
import hashlib
import json
import re
from pathlib import Path
from lift_frontend import Lift, PARSER, DATA_SYMBOLS, hardware_registers_row
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]
SYMBOLS = {}
for path in ('config/arm9/symbols.txt', 'config/arm9/itcm/symbols.txt',
             'external/sm64ds-pc/config/arm9/symbols.txt'):
    for line in (ROOT/path).read_text().splitlines():
        match = re.match(r'(\S+) kind:function.*?addr:0x([0-9a-f]+)', line)
        if match:
            SYMBOLS.setdefault(match[1], int(match[2], 16))
SYMBOLS['func_ov001_020ab2e4'] = 0x020ab2e4
# Overlay 1 (the resident common module) functions and data: original stage
# frames call its HUD/coin routines at their cartridge addresses.
for line in (ROOT/'external/sm64ds-pc/config/arm9/overlays/ov001/symbols.txt').read_text().splitlines():
    match = re.match(r'(\S+) kind:function.*?addr:0x([0-9a-f]+)', line)
    if match:
        SYMBOLS.setdefault(match[1], int(match[2], 16))
SYMBOLS['_ZN10dScStage_c18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc'] = 0x020fe190
SYMBOLS['_ZN6Player7SetAnimEjiit'] = 0x020bef2c
for line in (ROOT/'external/sm64ds-pc/config/arm9/overlays/ov002/symbols.txt').read_text().splitlines():
    match=re.match(r'(\S+) kind:(\w+).*?addr:0x([0-9a-f]+)',line)
    if match:
        if match[2]=='function': SYMBOLS[match[1]]=int(match[3],16)
        else: DATA_SYMBOLS[match[1]]=int(match[3],16)
# Castle-grounds overlays the stage loads (level ov009 and the actor
# modules of overlay_initializers below): their function names resolve to
# their cartridge addresses; bindings stay owned by their overlay.
for module in (9, 62, 69, 85, 89, 94, 98, 100, 102):
    for line in (ROOT/('external/sm64ds-pc/config/arm9/overlays/ov%03d/symbols.txt' % module)).read_text().splitlines():
        match=re.match(r'(\S+) kind:function.*?addr:0x([0-9a-f]+)',line)
        if match:
            SYMBOLS.setdefault(match[1],int(match[2],16))
# EU ov001: literal pool 0x020ab35c..0x020ab370 in the original reset.
STAGE_DATA = {'gCapIconFlashTimer':0x020ad620, 'gCapIconDispatchLocked':0x020ad624,
              'gCapIconRequestFlags':0x020ad628, 'gCapIconDispatchFlags':0x020ad62c,
              'gCapIconCounts':0x020ad630, 'gCapIconListHeads':0x020ad634}
DATA_SYMBOLS.update(STAGE_DATA)


class StageLift(Lift):
    def address(self,name):
        return STAGE_DATA[name] if name in STAGE_DATA else super().address(name)

TYPES = '''
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef long long s64;
typedef int Fix12i;
struct Matrix2x2 { int m[4]; };
struct SharedFilePtr { u16 fileID; u8 numRefs; u8 pad; void *filePtr; };
struct LVL_Overlay { u8 bytes[0x1c]; };
struct MeshCollider { u8 bytes[0x50]; };
struct SysTracker { u8 bytes[0x81c]; };
struct BMD_Bone;
struct BMD_File;
struct BMD_Material;
struct Matrix4x3 { s32 m[12]; };
struct ModelComponents {
    BMD_File *modelFile; BMD_Material *materials; BMD_Bone *bones;
    Matrix4x3 *transforms; void *unk_10;
};
struct Model {
    void *vtable; BMD_File *modelFile; ModelComponents data;
    Matrix4x3 mat4x3; void *transformsBuf;
};
struct StageTexAnimSlot { void *mTransformer; u8 mActive; u8 pad[3]; void *mBlockList; };
struct Stage {
    u8 base[0x50]; SysTracker mSysTracker; Model mModel;
    StageTexAnimSlot mTexAnimSlots[8]; MeshCollider mMeshCollider;
    u8 fog[0x50]; void *skybox; u8 pad9c0[4]; s32 mWaitingForLoad;
};
struct Fader { u8 bytes[0x60]; };
struct Heap { u8 opaque; };
struct SaveData { u32 magic; u32 flags1; s32 flags2; };
struct MinimapState { u8 bytes[0x24]; };
static_assert(sizeof(Stage)==0x9c8);
static_assert(offsetof(Stage,mSysTracker)==0x50);
static_assert(offsetof(Stage,mModel)==0x86c);
static_assert(offsetof(Stage,mTexAnimSlots)==0x8bc);
static_assert(offsetof(Stage,mMeshCollider)==0x91c);
static_assert(offsetof(Stage,mWaitingForLoad)==0x9c4);
static_assert(sizeof(SharedFilePtr)==8);
static_assert(sizeof(Model)==0x50);
static_assert(sizeof(ModelComponents)==0x14);
static_assert(offsetof(Model,data)==8);
static_assert(offsetof(Model,mat4x3)==0x1c);
static_assert(offsetof(Model,transformsBuf)==0x4c);
'''


def digest(path):
    return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()


def globals_for(body,rel=None):
    names = set(re.findall(r'\b(?:data_\w+|VS_STAR_SPAWN_ORDERS)\b', body))
    texts = [(ROOT/'src/game/stages/d_s_stage_methods.cpp').read_text(),
             (ROOT/'src/game/stages/d_scene.cpp').read_text(),
             (ROOT/'include/decl_common.h').read_text(),
             (ROOT/'src/engine/sound/Sound.cpp').read_text()]
    if rel: texts.insert(0,(ROOT/rel).read_text())
    found = {}
    for text in texts:
        for match in re.finditer(r'^extern [^;\n]+;', text, re.M):
            line = match.group()
            if '(' not in line:
                for name in names:
                    if re.search(r'\b'+name+r'\b',line) and name not in found:
                        found[name] = line
    extras = {'data_0209f314':'extern int data_0209f314;',
              'data_020a0eac':'extern Heap *data_020a0eac;',
              'data_0209f358':'extern u8 data_0209f358[];',
              'data_0209f34c':'extern u32 data_0209f34c;',
              'data_0209f5c0':'extern u32 data_0209f5c0;',
              'data_0209f1e4':'extern u32 data_0209f1e4;'}
    extras['data_020a0c80']='extern u32 data_020a0c80[0x18];'
    for name in names:
        if name in extras:
            found[name] = extras[name]
    if names-set(found):
        raise ValueError('missing stage global declarations: '+str(names-set(found)))
    return '\n'.join(found[name] for name in sorted(found))


def projected(rel, body, prototypes, exports, extra='', auto_globals=True):
    text = TYPES + extra + (globals_for(body,rel) if auto_globals else '') + '\n' + prototypes + '\n' + body
    lift = StageLift(text)
    result = lift.result()
    # Replace only imported declarations with exact, typed native call veneers.
    # Complete definitions above keep their entire original control flow.
    data = result.encode()
    edits = []
    for node in PARSER.parse(data).root_node.named_children:
        declaration = node.child_by_field_name('body') if node.type == 'linkage_specification' else node
        if declaration is None or declaration.type != 'declaration':
            continue
        spelling = data[node.start_byte:node.end_byte].decode()
        match = re.fullmatch(r'extern "C" (.+?) (pk_\w+)\((.*)\);', spelling, re.S)
        if not match:
            continue
        ret, name, params = match.groups()
        original = name.removeprefix('pk_')
        if original in exports:
            continue
        if original not in SYMBOLS:
            raise ValueError('unresolved stage import: '+original)
        # Parameter spellings are generated by Lift. Parse them as types and
        # retain all pointer cells; no host address is converted to a word.
        parsed = Lift('void probe('+params+');')
        decl = parsed.root.named_children[0].child_by_field_name('declarator')
        values = decl.child_by_field_name('parameters').named_children
        types = []
        for param in values:
            if param.type == 'parameter_declaration':
                _,typ = parsed.decode(param.child_by_field_name('declarator'), parsed.base(param))
                t = parsed.typename(typ)
                if t != 'void': types.append(t)
        # All import declarations below deliberately omit parameter names.
        args = ', '.join(t+' a'+str(i) for i,t in enumerate(types))
        call = 'Ptr<'+ret+'('+', '.join(types)+')>(0x%08xu)'%SYMBOLS[original]
        call += '('+', '.join('a'+str(i) for i in range(len(types)))+');'
        replacement = 'static '+ret+' '+name+'('+args+') { '+('' if ret=='void' else 'return ')+call+' }'
        edits.append((node.start_byte,node.end_byte,replacement))
    for start,end,replacement in reversed(edits):
        data = data[:start]+replacement.encode()+data[end:]
    dependencies = json.loads((ROOT/'android/tools/stage_runtime_profile.json').read_text())['inputs']
    dependencies.update({'include/decl_common.h':digest('include/decl_common.h'),
                         'src/engine/sound/Sound.cpp':digest('src/engine/sound/Sound.cpp')})
    return {'path':rel, 'sha256':digest(rel), 'header':False,
            'dependencies':dependencies, 'source':data.decode()}, [
                {'address':SYMBOLS[name], 'symbol':'abi_'+name} for name in exports]


def sound():
    rel = 'src/engine/sound/Sound.cpp'
    body = function((ROOT/rel).read_text(), 'extern "C" void func_02011b7c(void)')
    group=function((ROOT/rel).read_text(),'void LoadGroupAndSetBank(int a, int b)')
    group=replace_once(group,'void LoadGroupAndSetBank(int a, int b)',
                       'void _ZN5Sound19LoadGroupAndSetBankEii(int a, int b)')
    prototypes='''
void func_0204fa2c(int *,int); int func_0203d974(void); void func_0203d7d4(int);
int func_020134d8(void *,void *); int func_0205117c(int); int func_020510a4(int,int);
'''
    return projected(rel,body+'\n'+group,prototypes,
                     ['func_02011b7c','_ZN5Sound19LoadGroupAndSetBankEii'])


def narc():
    rel = 'src/engine/sound/SoundEngine.cpp'
    raw = (ROOT/rel).read_text()
    symbols = ['func_0204ef9c','func_0204ee40','func_0204ee10','func_0204ede8']
    signatures = ['extern "C" int func_0204ef9c(void* p)',
                  'extern "C" int func_0204ee40(struct S82 *self, char *name, char *p)',
                  'extern "C" int func_0204ee10(void *thiz)',
                  'extern "C" int func_0204ede8(struct S82 *o, unsigned int i)']
    body = '\n'.join(function(raw,s) for s in signatures)
    structs = re.search(r'struct S82 \{.*?\n\};',raw,re.S).group()+'\n'
    structs += re.search(r'struct NarcHeader \{.*?\n\};',raw,re.S).group()+'\n'
    declarations = '''
int func_0204ef9c(void *); void func_0205cd34(void *);
int func_0205cc80(void *,void *,void *);
int _ZN4cstd6strlenEPKc(const char *);
int func_0205cb68(void *,int,int,int,int,int,void *,void *);
void func_0205cbe4(void *); int func_0205caa8(void *);
'''
    return projected(rel,body,declarations,symbols,extra=structs)


def scene_graphics():
    rel = 'src/game/stages/d_scene.cpp'
    raw = (ROOT/rel).read_text()
    specs = [('void dScene_c::ResetHardwareRegisters()', '_ZN8dScene_c22ResetHardwareRegistersEv'),
             ('void dScene_c::Initialise3dGraphics()', '_ZN8dScene_c20Initialise3dGraphicsEv'),
             ('void dScStage_c::SetVramBanks()', '_ZN10dScStage_c12SetVramBanksEv'),
             ('void DisableAllBanks(){', '_ZN2GX15DisableAllBanksEv')]
    bodies = []
    for signature,symbol in specs:
        text = function(raw,signature)
        text = replace_once(text,signature,'void '+symbol+'()'+('{' if signature.endswith('{') else ''))
        bodies.append(text)
    body = '\n'.join(bodies)
    for old,new in {
        'GX::DisableAllBanks':'_ZN2GX15DisableAllBanksEv',
        'GX::SetGraphicsMode':'_ZN2GX15SetGraphicsModeEiii',
        'GXS::SetGraphicsMode':'_ZN3GXS15SetGraphicsModeEi',
        'G2x::SetBGyAffine':'_ZN3G2x12SetBGyAffineEPVtP9Matrix2x2iiii',
        '::Initialise3dGraphics':'Initialise3dGraphics',
        'ResetHardwareRegisters()':'_ZN8dScene_c22ResetHardwareRegistersEv()',
    }.items(): body = body.replace(old,new)
    for name in sorted(('BG','OBJ','SubBG','SubBGExtPltt','SubOBJ','Tex','TexPltt'),key=len,reverse=True):
        old = 'GX::SetBankFor'+name
        body = body.replace(old,'_ZN2GX'+str(len('SetBankFor'+name))+'SetBankFor'+name+'Et')
    prototypes = '''
void _ZN8dScene_c22ResetHardwareRegistersEv(void);
void _ZN2GX15DisableAllBanksEv(void);
void _ZN2GX15SetGraphicsModeEiii(int,int,int); void _ZN3GXS15SetGraphicsModeEi(int);
void _ZN3G2x12SetBGyAffineEPVtP9Matrix2x2iiii(volatile u16 *,Matrix2x2 *,int,int,int,int);
void func_02053c10(int); void func_02053be0(int); void func_02053a90(u16 *,int);
void func_0205583c(void); void Initialise3dGraphics(int);
'''
    for name in ('BG','OBJ','SubBG','SubBGExtPltt','SubOBJ','Tex','TexPltt'):
        prototypes += 'void _ZN2GX'+str(len('SetBankFor'+name))+'SetBankFor'+name+'Et(u16);\n'
    for address in ('02053f58','02054018','02054004','02053f6c','02053fa8',
                    '02053f94','02053f80','02053fe0','02053fbc','02053f44',
                    '02053f30','02053f08','02053ee0'):
        prototypes += 'u16 func_'+address+'(void);\n'
    return projected(rel,body,prototypes,[symbol for _,symbol in specs])


def free_c(rel):
    raw = re.sub(r'^#include[^\n]*','',(ROOT/rel).read_text(),flags=re.M)
    if '#include "nitro/fx/fx.h"' in (ROOT/rel).read_text():
        raw='#include "nitro/fx/fx.h"\n'+raw
    shared = (ROOT/'include/decl_common.h').read_text()
    used = set(re.findall(r'\b(?:data_\w+|func_\w+|SUBLEVEL_LEVEL_TABLE)\b', raw))
    imports = [m.group() for m in re.finditer(r'^extern [^;\n]+;',shared,re.M)
               if '#include "decl_common.h"' in (ROOT/rel).read_text()
               and set(re.findall(r'\b(?:data_\w+|func_\w+|SUBLEVEL_LEVEL_TABLE)\b',m.group())) & used]
    raw = '\n'.join(imports)+'\n'+raw
    if rel in ('src/Matrix4x3_ApplyInPlaceToRotationX.c','src/Matrix4x3_ApplyInPlaceToScale.c',
               'src/Matrix4x3_ApplyInPlaceToRotationZ.c','src/Matrix4x3_ApplyInPlaceToTranslation.c'):
        raw=replace_once(raw,'struct Matrix4x3 { char data[0x30]; };','')
    if rel.endswith('port/unmatched/func_ov002_020d3b9c.c'):
        # ITCM 0x01ffabe4 is the cartridge's __aeabi_idiv symbol.
        raw = raw.replace('func_01ffabe4', '__aeabi_idiv')
    if rel.endswith('/func_0205cb68.c'):
        raw = replace_once(raw,'extern void func_0205d2dc(void);',
                           'extern int func_0205d2dc(void *,int,int,int);')
        raw = replace_once(raw,'extern void func_0205d2b0(void);',
                           'extern int func_0205d2b0(void *,const void *,unsigned int,unsigned int);')
    if rel == 'src/FS_LoadOverlay.c':
        # This symbol is an absolute DS RAM boundary, never a callable native
        # function. Preserve the original numeric comparison at host width.
        raw = replace_once(raw,'extern void func_ov001_020aa420(void);','')
        raw = replace_once(raw,'(unsigned int)func_ov001_020aa420','0x020aa420u')
    if rel.endswith('/func_02018b64.c'):
        raw = replace_once(raw,'extern int func_02018dc4(void *a, Pair bc);',
                           'extern int func_02018dc4(void *, int, int);')
        raw = replace_once(raw,'func_02018dc4(&file, v)',
                           'func_02018dc4(&file, v.a, v.b)')
    if rel.endswith('/func_02018dc4.c'):
        # ARM passes the eight-byte FileID as two argument words. Native ABI
        # locals b/c need not be adjacent, so reconstruct the actual record.
        raw = replace_once(raw,'int func_02018dc4(int a, int b, int c, ...)',
                           'int func_02018dc4(int a, int b, int c)')
        raw = replace_once(raw,'int r = func_0205d568(a, *(struct BC *)&b);',
                           'struct BC id = {b,c};\n    int r = func_0205d568(a, id);')
    if rel.endswith('/func_0206d32c.c'):
        raw = replace_once(raw,'? 0 : data_020a9ec0[type]',
                           '? (struct Arr *)0 : data_020a9ec0[type]')
        raw = replace_once(raw,'#define LM(x) ((int*)(long long)(int)(x))',
                           '#define LM(x) ((int*)(x))')
        raw = replace_once(raw,'#define LM2(x) ((int*)(long long)(unsigned)(x))',
                           '#define LM2(x) ((int*)(x))')
    lift = Lift(raw)
    lift.result()
    exports = [name for name,_,_ in lift.exports]
    return projected(rel,raw,'',exports,auto_globals=False)


def graphics_3d():
    rel = 'src/engine/view/Graphics3D.cpp'
    raw = (ROOT/rel).read_text()
    body = function(raw,'void Initialise3dGraphics(int arg) {')
    body += '\n'+function(raw,'void CopyToViewMat(const Matrix4x3 *src)')
    body=body.replace('void CopyToViewMat(', 'void _Z13CopyToViewMatPK9Matrix4x3(')
    body = body.replace('G3X::SetClearColor','_ZN3G3X13SetClearColorEtiiib')
    macros = '\n'.join(line for line in raw.splitlines() if line.startswith('#define REG_'))
    prototypes = '''
void func_0205583c(void); void func_020554bc(void); void func_020556d0(void);
void _ZN3G3X13SetClearColorEtiiib(u16,int,int,int,bool);
void _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,int,void *);
void InvMat4x3(Matrix4x3 *,Matrix4x3 *);
'''
    return projected(rel,body,prototypes,['Initialise3dGraphics','_Z13CopyToViewMatPK9Matrix4x3'],extra=macros+'\n')


def controller_text():
    rel='src/Message.cpp'
    raw=(ROOT/rel).read_text()
    body='\n'.join(function(raw,signature) for signature in (
        'extern "C" void LoadControllerModeText(int param0)',
        'extern "C" void func_0201d590(void)'))
    body=body.replace('G2::GetBG2ScrPtr','_ZN2G212GetBG2ScrPtrEv')
    body=body.replace('G2::GetBG2CharPtr','_ZN2G213GetBG2CharPtrEv')
    prototypes='''
void func_0201eaac(void); void func_0201d590(void);
char *_ZN2G212GetBG2ScrPtrEv(void); void *_ZN2G213GetBG2CharPtrEv(void);
void func_0201b6f8(int); void func_0201b100(int); void MultiStore_Int(int,int *,int);
'''
    return projected(rel,body,prototypes,['LoadControllerModeText','func_0201d590'],
                     extra='struct StarEntry;\n')


def controller_glyphs():
    rel='src/Message2.cpp'
    raw=(ROOT/rel).read_text()
    body='\n'.join(function(raw,signature) for signature in (
        'void func_0201b100(int mode) {','void func_0201b6f8(int mode) {'))
    body=body.replace('G2::GetBG2CharPtr','_ZN2G213GetBG2CharPtrEv')
    body=body.replace('G2S::GetBG0CharPtr','_ZN3G2S13GetBG0CharPtrEv')
    # Pixel rows are 32-bit words. The ARM register shifter returns zero for
    # logical shifts of 32; C++ otherwise has UB (and x86 masks the count).
    data=body.encode()
    def shifts(node):
        if node.type=='binary_expression':
            operator=node.child_by_field_name('operator')
            if operator and data[operator.start_byte:operator.end_byte] in (b'<<',b'>>'):
                name='stage_lsl32' if data[operator.start_byte:operator.end_byte]==b'<<' else 'stage_lsr32'
                return name+'((u32)('+shifts(node.child_by_field_name('left'))+'),(u32)('+shifts(node.child_by_field_name('right'))+'))'
        result=[];at=node.start_byte
        for child in node.named_children:
            result.extend((data[at:child.start_byte].decode(),shifts(child)));at=child.end_byte
        result.append(data[at:node.end_byte].decode())
        return ''.join(result)
    body=shifts(PARSER.parse(data).root_node)
    helpers='''
static u32 stage_lsl32(u32 value,u32 shift) { return shift<32 ? value<<shift : 0; }
static u32 stage_lsr32(u32 value,u32 shift) { return shift<32 ? value>>shift : 0; }
'''
    prototypes='''
int func_02054d88(void); void *_ZN2G213GetBG2CharPtrEv(void);
u32 _ZN3G2S13GetBG0CharPtrEv(void); void MultiCopy_Int(int *,int *,int);
'''
    return projected(rel,body,prototypes,['func_0201b100','func_0201b6f8'],extra=helpers)


def collision_reset():
    rel='src/engine/collision/d_bg_w.cpp'
    body=function((ROOT/rel).read_text(),'extern "C" void func_02039218(void)')
    return projected(rel,body,'',['func_02039218'])


def cap_reset():
    rel='src/ui/hud/d_cap_icon.cpp'
    body=function((ROOT/rel).read_text(),'extern "C" void func_ov001_020ab2e4(void)')
    body=body.replace('SaveData::IsCharacterUnlocked','_ZN8SaveData19IsCharacterUnlockedEj')
    declarations='''
extern u8 gCapIconFlashTimer; extern u8 gCapIconDispatchLocked;
extern u8 gCapIconRequestFlags[3]; extern u8 gCapIconDispatchFlags[3];
extern u8 gCapIconCounts[4]; extern void *gCapIconListHeads[3];
int _ZN8SaveData19IsCharacterUnlockedEj(u32);
'''
    return projected(rel,body,declarations,['func_ov001_020ab2e4'])


def save_predicate():
    rel='src/engine/save/SaveData.cpp'
    body=function((ROOT/rel).read_text(),'s32 SaveData::IsCharacterUnlocked(u32 character)')
    body=body.replace('SaveData::IsCharacterUnlocked','_ZN8SaveData19IsCharacterUnlockedEj')
    body+='\n'+function((ROOT/rel).read_text(),'int SaveData::NumGlowingRabbitsFound()')
    body=body.replace('SaveData::NumGlowingRabbitsFound','_ZN8SaveData22NumGlowingRabbitsFoundEv')
    for name in ('CanPlayerHaveCap','HasPlayerLostCap'):
        body+='\n'+function((ROOT/rel).read_text(),'int SaveData::'+name+'()')
    for name in ('CanPlayerHaveCap','HasPlayerLostCap'):
        body=body.replace('SaveData::'+name,'_ZN8SaveData%d%sEv'%(len(name),name))
    body=body.replace('((SaveData*)data_0209caa0)->mCharacter','(*(u8 *)((char *)data_0209caa0+0x41))')
    return projected(rel,body,'',['_ZN8SaveData19IsCharacterUnlockedEj',
                                 '_ZN8SaveData22NumGlowingRabbitsFoundEv',
                                 '_ZN8SaveData16CanPlayerHaveCapEv',
                                 '_ZN8SaveData16HasPlayerLostCapEv'])


def shared_file_handles():
    rel='src/SharedFilePtr.cpp'
    raw=(ROOT/rel).read_text()
    signatures={
        'func_02017e48':'extern "C" void *func_02017e48(void *self, unsigned int fileID)',
        'func_02017e34':'extern "C" void *func_02017e34(void *self)',
        'func_02017e0c':'extern "C" void func_02017e0c(void *self, unsigned int fileID)',
        'func_02017acc':'extern "C" void *func_02017acc(void *self, unsigned int fileID)',
        'func_02017b4c':'extern "C" void *func_02017b4c(void *self, unsigned int fileID)',
        'func_02017ab4':'extern "C" int func_02017ab4(int self)',
        'SharedFilePtr_Destruct_Anim':'extern "C" int SharedFilePtr_Destruct_Anim(int self)',
        'SharedFilePtr_Destruct_Clsn':'extern "C" int SharedFilePtr_Destruct_Clsn(int self)',
        'SharedFilePtr_Destruct_TexSeq':'extern "C" int SharedFilePtr_Destruct_TexSeq(int self)',
        'SharedFilePtr_Construct_TexSeq':'extern "C" void *SharedFilePtr_Construct_TexSeq(void *self, unsigned int fileID)',
        'func_02017c24':'extern "C" void func_02017c24(SharedFilePtr *self)',
    }
    body='\n'.join(function(raw,signature) for signature in signatures.values())
    construct=function(raw,'SharedFilePtr &SharedFilePtr::Construct(unsigned int fileID)')
    construct=replace_once(construct,'SharedFilePtr &SharedFilePtr::Construct(unsigned int fileID)',
                           'SharedFilePtr *_ZN13SharedFilePtr9ConstructEj(SharedFilePtr *self, unsigned int fileID)')
    construct=replace_once(construct,'func_02017e48(this, fileID);','func_02017e48(self, fileID);')
    construct=replace_once(construct,'return *this;','return self;')
    body+='\n'+construct
    methods={
        '_ZN13SharedFilePtr8LoadFileEv':('void *SharedFilePtr::LoadFile()', 'void *'),
        '_ZN13SharedFilePtr7ReleaseEv':('void SharedFilePtr::Release()', 'void'),
        '_ZN13SharedFilePtr19ReallocateModelFileEv':('void SharedFilePtr::ReallocateModelFile()', 'void'),
    }
    for symbol,(signature,ret) in methods.items():
        method=function(raw,signature)
        method=replace_once(method,signature,ret+' '+symbol+'(SharedFilePtr *self)')
        for field in ('fileID','numRefs','filePtr'):
            method=re.sub(r'\b'+field+r'\b','self->'+field,method)
        method=method.replace('Load()','_ZN13SharedFilePtr4LoadEv(self)')
        method=method.replace('func_02017c24(this)','func_02017c24(self)')
        body+='\n'+method
    model=function(raw,'void *dExtSimpleModel_c::LoadFile(SharedFilePtr &ptr)')
    model=replace_once(model,'void *dExtSimpleModel_c::LoadFile(SharedFilePtr &ptr)',
                       'void *_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(SharedFilePtr *ptr)')
    model=model.replace('ptr.LoadFile()','_ZN13SharedFilePtr8LoadFileEv(ptr)')
    model=model.replace('ptr.filePtr','ptr->filePtr').replace('ptr.numRefs','ptr->numRefs')
    model=model.replace('UpdateFileOffsets(*(BMD_File *)filePtr)',
                        '_ZN17dExtSimpleModel_c17UpdateFileOffsetsER8BMD_File(filePtr)')
    model=model.replace('AddToCommonModelDataArr(*(BMD_File *)filePtr)',
                        '_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(filePtr)')
    model=model.replace('ptr.ReallocateModelFile()','_ZN13SharedFilePtr19ReallocateModelFileEv(ptr)')
    body+='\n'+model
    animation_loaders=[]
    for cls,ret,kind in [('dExtFrameCtrl_c','char *','BCA'),('dExtAnmTexPat_c','void *','BTP'),
                         ('dBgW_Kc','char *','KCL')]:
        symbol='_ZN'+str(len(cls))+cls+'8LoadFileER13SharedFilePtr'
        signature=ret+cls+'::LoadFile(SharedFilePtr &ptr)'
        loader=replace_once(function(raw,signature),signature,
                             ret+symbol+'(SharedFilePtr *ptr)')
        loader=loader.replace('ptr.LoadFile()','_ZN13SharedFilePtr8LoadFileEv(ptr)')
        loader=loader.replace('ptr.numRefs','ptr->numRefs').replace('ptr.filePtr','ptr->filePtr')
        update='_ZN'+str(len(cls))+cls+'17UpdateFileOffsetsER8'+kind+'_File'
        loader=loader.replace('UpdateFileOffsets(*('+kind+'_File *)file)',update+'(file)')
        loader=loader.replace('UpdateFileOffsets(*('+kind+'_File *)filePtr)',update+'(filePtr)')
        body+='\n'+loader
        animation_loaders.append(symbol)
    body=body.replace('Memory::Deallocate','_ZN6Memory10DeallocateEPv')
    body=body.replace('static_cast<SharedFilePtr *>(self)','((SharedFilePtr *)self)')
    body=body.replace('reinterpret_cast<void *>(self)','((void *)self)')
    prototypes='''
void func_02017e0c(void *,u32); void *func_02017e48(void *,u32);
void *func_02017e34(void *); u32 func_02018a24(u32);
void *_ZN13SharedFilePtr4LoadEv(SharedFilePtr *);
void *_ZN13SharedFilePtr8LoadFileEv(SharedFilePtr *);
void _ZN13SharedFilePtr19ReallocateModelFileEv(SharedFilePtr *);
void func_02017c24(SharedFilePtr *); void _ZN6Memory10DeallocateEPv(void *);
u32 func_02017060(void *);
void _ZN17dExtSimpleModel_c17UpdateFileOffsetsER8BMD_File(void *);
void *_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(void *);
void _ZN15dExtFrameCtrl_c17UpdateFileOffsetsER8BCA_File(void *);
void _ZN15dExtAnmTexPat_c17UpdateFileOffsetsER8BTP_File(void *);
void _ZN7dBgW_Kc17UpdateFileOffsetsER8KCL_File(void *);
'''
    return projected(rel,body,prototypes,[*signatures,*methods,*animation_loaders,
                     '_ZN13SharedFilePtr9ConstructEj','_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr'])


def common_models():
    rel='src/engine/model/d_ext_simple_model.cpp'
    raw=(ROOT/rel).read_text()
    add=function(raw,'void *dExtSimpleModel_c::AddToCommonModelDataArr(BMD_File &file)')
    add=replace_once(add,'void *dExtSimpleModel_c::AddToCommonModelDataArr(BMD_File &file)',
                     'void *_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(void *file)')
    add=add.replace('&file','file').replace('LoadTexAndPal(file)','_ZN17dExtSimpleModel_c13LoadTexAndPalER8BMD_File(file)')
    trim=function(raw,'extern "C" u32 func_02017060(void *ptr)')
    trim=replace_once(trim,'((Heap *)heap)->_Sizeof(ptr)',
                      '_ZN5mHeap6Heap_t7_SizeofEPv((Heap *)heap,ptr)')
    trim=replace_once(trim,'((Heap *)heap)->Reallocate(ptr, newSize)',
                      '_ZN5mHeap6Heap_t10ReallocateEPvj((Heap *)heap,ptr,newSize)')
    pooled=function(raw,'extern "C" void *func_02016e70(void *file)')
    pooled=pooled.replace('dExtSimpleModel_c::AddToCommonModelDataArr(*(BMD_File *)file)',
        '_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(file)')
    pooled=pooled.replace('::operator new(0x14)','_Znwj(0x14)')
    pooled=pooled.replace('Memory::operator_new2','_ZN6Memory13operator_new2Ej')
    pooled=pooled.replace('Memory::operator_delete2','_ZN6Memory16operator_delete2EPv')
    pooled=re.sub(r'#ifdef _MSC_VER.*?#else\n(.*?)#endif',r'\1',pooled,flags=re.S)
    prototypes='''
void _ZN17dExtSimpleModel_c13LoadTexAndPalER8BMD_File(void *);
u32 func_020469e0(void *); u32 _ZN5mHeap6Heap_t7_SizeofEPv(Heap *,void *);
u32 _ZN5mHeap6Heap_t10ReallocateEPvj(Heap *,void *,u32);
void *_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(void *);
void *_Znwj(u32);
void *_ZN6Memory13operator_new2Ej(u32); void _ZN6Memory16operator_delete2EPv(void *);
int func_02046564(void *); void func_020462d0(ModelComponents *,void *,void *);
'''
    return projected(rel,add+'\n'+trim+'\n'+pooled,prototypes,
                     ['_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File','func_02017060','func_02016e70'],
                     extra='struct CommonModelData { void *file; ModelComponents *components; void *buffer; };\n')


def model_instances():
    rel='src/engine/model/d_ext_simple_model.cpp'
    raw=(ROOT/rel).read_text()
    specs=[('void dExtSimpleModel_c::LoadAndSetFile(u16 ov0ID, int a, int b)',
            'void _ZN17dExtSimpleModel_c14LoadAndSetFileEtii(Model *self,u16 ov0ID,int a,int b)'),
           ('int dExtSimpleModel_c::DoSetFile(char *file_, int a, int b)',
            'int _ZN17dExtSimpleModel_c9DoSetFileEPcii(Model *self,char *file_,int a,int b)'),
           ('extern "C" void func_02016b24(dExtSimpleModel_c *model, u32 mask)',
            'void func_02016b24(Model *model,u32 mask)')]
    bodies=[]
    for signature,replacement in specs:
        body=replace_once(function(raw,signature),signature,replacement)
        body=body.replace('::LoadFile','LoadFile').replace('this','self')
        body=body.replace('modelFile = file','self->modelFile = file')
        body=body.replace('AddToCommonModelDataArr(*file)',
            '_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(file)')
        body=body.replace('Memory::operator_new2','_ZN6Memory13operator_new2Ej')
        body=re.sub(r'(?<!->)\btransformsBuf\b','self->transformsBuf',body)
        body=body.replace('&data,','&self->data,')
        body=body.replace('data.UpdateVertsUsingBones()',
            '_ZN15ModelComponents21UpdateVertsUsingBonesEv(&self->data)')
        body=body.replace('SetPolygonID(b & 0xff)','_ZN17dExtSimpleModel_c12SetPolygonIDEi(self,b & 0xff)')
        bodies.append(body)
    setfile=function(raw,'extern "C" int func_02016ff4(dExtModel_c *self, BMD_File *file, int c, int d)')
    setfile=setfile.replace('dExtModel_c *self','Model *self')
    setfile=setfile.replace('dExtSimpleModel_c::UpdateFileOffsets(*file)',
        '_ZN17dExtSimpleModel_c17UpdateFileOffsetsER8BMD_File(file)')
    setfile=setfile.replace('self->DoSetFile((char *)file, c, d)',
        '((int (*)(Model *,char *,int,int))((u32 *)self->vtable)[2])(self,(char *)file,c,d)')
    bodies.append(setfile)
    # Use the full original pointer-bearing file records, projected by Lift.
    header=(ROOT/'include/BMD_File.h').read_text()
    records=header[header.index('struct BMD_Bone {'):header.index('#ifndef SM64DS_PLATFORM_PC')]
    declarations='''
void *LoadFile(u16); int func_02016ff4(Model *,BMD_File *,int,int);
void _ZN17dExtSimpleModel_c17UpdateFileOffsetsER8BMD_File(BMD_File *);
void *_ZN17dExtSimpleModel_c23AddToCommonModelDataArrER8BMD_File(BMD_File *);
void *_ZN6Memory13operator_new2Ej(u32); u32 func_02017060(void *);
int func_02046564(BMD_File *); void func_020462d0(ModelComponents *,BMD_File *,void *);
void _ZN15ModelComponents21UpdateVertsUsingBonesEv(ModelComponents *);
void func_02016b24(Model *,u32);
void _ZN17dExtSimpleModel_c12SetPolygonIDEi(Model *,int);
'''
    return projected(rel,'\n'.join(bodies),declarations,
        ['_ZN17dExtSimpleModel_c14LoadAndSetFileEtii','_ZN17dExtSimpleModel_c9DoSetFileEPcii',
         'func_02016b24','func_02016ff4'],extra=records)


def stage_model():
    rel='src/game/stages/d_s_stage_methods.cpp'
    body=function((ROOT/rel).read_text(),'void dScStage_c::LoadModel()')
    body=body.replace('void dScStage_c::LoadModel()',
                      'void _ZN10dScStage_c9LoadModelEv(Stage *self)')
    body=body.replace('mModel.LoadAndSetFile(',
        '_ZN17dExtSimpleModel_c14LoadAndSetFileEtii(&self->mModel,')
    body=body.replace('&mModel.data','&self->mModel.data')
    header=(ROOT/'include/BMD_File.h').read_text()
    records=header[header.index('struct BMD_Bone {'):header.index('#ifndef SM64DS_PLATFORM_PC')]
    return projected(rel,body,
        'void _ZN17dExtSimpleModel_c14LoadAndSetFileEtii(Model *,u16,int,int);',
        ['_ZN10dScStage_c9LoadModelEv'],extra=records)


def level_objects():
    rel='src/game/stages/LevelObjects.cpp'
    raw=(ROOT/rel).read_text()
    raw=re.sub(r'^#include[^\n]*','',raw,flags=re.M)
    # Keep every category loader and its full dispatch/selection body.
    names=re.findall(r'// @symbol (\S+)',raw)
    method='_ZN10dScStage_c18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc'
    for name in names:
        if name==method: continue
        # The @symbol immediately preceding each definition owns its name.
        pattern=r'(// @symbol '+re.escape(name)+r'\s*\n)void (\w+)\('
        found=re.search(pattern,raw)
        if not found: raise ValueError('level category definition changed: '+name)
        short=found[2]
        raw=re.sub(r'\b'+short+r'\b',name,raw)
    raw=raw.replace('void dScStage_c::LoadClsnAndObjects(LVL_Overlay &overlayRef, u32 entranceIndex,\n                                    dBgW_Kc &meshColliderRef)',
        'void '+method+'(LVL_Overlay *overlayRef,u32 entranceIndex,MeshCollider *meshColliderRef)')
    raw=raw.replace('&overlayRef','overlayRef').replace('&meshColliderRef','meshColliderRef')
    raw=raw.replace('LVL_Overlay::','')
    # LoadExitObjects (ov002 0x020fe48c/0x020fe49c) stores only rot.x/rot.y;
    # the cartridge passes whatever its stack slot sp+0xc held as rot.z. A
    # native stack has no such reproducible value: define it as 0, so exit
    # actors are deterministic across builds instead of reading host garbage.
    exit_rot='        rot.y = -entry->rotY;\n'
    if raw.count(exit_rot)!=1: raise ValueError('LoadExitObjects rotation changed')
    raw=raw.replace(exit_rot,exit_rot+'        rot.z = 0;\n')
    raw=raw.replace('&table','*table').replace('table.','table->')
    raw=re.sub(r'\bdBgW_Kc\b','MeshCollider',raw).replace('dBgW *','MeshCollider *')
    raw=raw.replace('MeshCollider::UpdateFileOffsets(*collisionFile)',
        '_ZN7dBgW_Kc17UpdateFileOffsetsER8KCL_File(collisionFile)')
    raw=raw.replace('((MeshCollider *)meshCollider)->SetFile(collisionFile, *overlay->clps)',
        '_ZN7dBgW_Kc7SetFileEP8KCL_FileR10CLPS_Block(meshCollider,collisionFile,overlay->clps)')
    raw=raw.replace('((MeshCollider *)meshCollider)->GetOctreeOriginY()',
        '_ZNK7dBgW_Kc16GetOctreeOriginYEv(meshCollider)')
    raw=raw.replace('((MeshCollider *)meshCollider)->GetUnkOctreeY()',
        '_ZNK7dBgW_Kc13GetUnkOctreeYEv(meshCollider)')
    raw=raw.replace('((MeshCollider *)(meshCollider))->Enable((dActor_c *)(0))',
        '_ZN4dBgW6EnableEP8dActor_c(meshCollider,0)')
    raw=raw.replace('dBase_c::Spawn','_ZN7dBase_c5SpawnEjP7fBase_cii')
    raw=raw.replace('PathPtr::SetNodeTable','_ZN7PathPtr12SetNodeTableEPv')
    raw=raw.replace('PathPtr::SetPathTable','_ZN7PathPtr12SetPathTableEP7PathDefi')
    # References become checked four-byte addresses. Do not form a host
    # reference to one of these cartridge table records.
    raw=re.sub(r'(_Z\w+Load\w*Objects\w*\()\*([^,\n]+)',r'\1\2',raw)
    raw=raw.replace('_Z11LoadObjectsRN11LVL_Overlay8ObjTableEij(*overlay->objTable',
        '_Z11LoadObjectsRN11LVL_Overlay8ObjTableEij(overlay->objTable')
    raw=raw.replace('_Z11LoadObjectsRN11LVL_Overlay8ObjTableEij(*subTable->table',
        '_Z11LoadObjectsRN11LVL_Overlay8ObjTableEij(subTable->table')
    header=(ROOT/'include/LVL_Overlay.h').read_text()
    records='\n'.join(re.search(r'    struct '+name+r' \{.*?\n    \};',header,re.S)[0]
        for name in ('ObjTable','ObjSubTable','ExitEntry','TeleportSourceEntry','StandardEntry','SimpleEntry'))
    extra='''
typedef int Fix12i;
struct Vector3 { s32 x,y,z; }; struct Vector3s { s16 x,y,z; };
typedef Vector3s Vector3_16;
struct fBase_c; struct dActor_c; struct CLPS_Block; struct KCL_File;
struct PathDef;
'''+records+'''
static_assert(sizeof(ObjTable)==8 && sizeof(ObjSubTable)==8);
static_assert(sizeof(StandardEntry)==16 && sizeof(SimpleEntry)==8);
struct LevelSubTable { ObjTable *table; char pad_004[8]; };
'''
    raw=raw.replace('struct SubTbl {\n        ObjTable *table;\n        char pad_004[8];\n    } *subTables;', 'LevelSubTable *subTables;')
    raw=raw.replace('LVL_Overlay_Layout::SubTbl','LevelSubTable')
    declarations='''
void _ZN7PathPtr12SetNodeTableEPv(void *); void _ZN7PathPtr12SetPathTableEP7PathDefi(PathDef *,int);
void *_ZN7dBase_c5SpawnEjP7fBase_cii(u32,fBase_c *,int,int);
void _ZN7dBgW_Kc17UpdateFileOffsetsER8KCL_File(KCL_File *);
void _ZN7dBgW_Kc7SetFileEP8KCL_FileR10CLPS_Block(MeshCollider *,KCL_File *,CLPS_Block *);
void _ZN4dBgW6EnableEP8dActor_c(MeshCollider *,dActor_c *);
int _ZNK7dBgW_Kc16GetOctreeOriginYEv(MeshCollider *); int _ZNK7dBgW_Kc13GetUnkOctreeYEv(MeshCollider *);
int ContinueKuppaScriptIfNecessary(void); void StartIntroCutscene(void);
void func_0202a850(int,int);
extern short data_ov002_0211118c;
'''
    row,bindings=projected(rel,raw,declarations,names,extra=extra,auto_globals=False)
    for binding in bindings: binding['overlay']=2
    return row,bindings


def player_init():
    rel='src/game/player/Player.cpp'
    body=function((ROOT/rel).read_text(),'s32 Player::InitResources()')
    body=replace_once(body,'s32 Player::InitResources()',
                       's32 _ZN6Player13InitResourcesEv(void *object)')
    body=body.replace('(void*)this','object')
    # The empty retail Vector3 constructor was under-declared as void(void).
    # Array callbacks still carry 'this' in r0; preserve it in the native thunk.
    body=body.replace('void func_0203d384(void);','void *func_0203d384(void *);')
    body=body.replace('((Player *)(c))->func_ov002_020beabc()',
                       'func_ov002_020beabc(c)')
    body=body.replace('((Player *)(c))->func_ov002_020c7dd0(n8)',
                       'func_ov002_020c7dd0(c,n8)')
    body=body.replace('((dBgCh_Gnd *)rc)->mProbeHeight','(*(s32 *)(rc+0x4c))')
    body=body.replace('__builtin__clear((void*)mHeldObjQueue, 0x14)',
                       'func_0205a588((void*)mHeldObjQueue,0,0x14)')
    # Named lanes of the original Player/dActor/fBase records. Their source
    # headers stay unchanged; every byte offset remains checked by the profile.
    fields={
        'param1':('u32',8),'mPlayerNo':('u8',0x6d8),'mCharacter':('u8',0x6d9),
        'mTrailPos':('Vector3 *',0x578),'mTrailFlags':('u8 *',0x57c),
        'mHeldObjQueue':('s32',0x588),'mMeshClsn':('StageMeshQuery',0x380),
        'mScaleX':('s32',0x80),'mScaleY':('s32',0x84),'mScaleZ':('s32',0x88),
        'mTerminalVelocity':('s32',0xa0),'mPosX':('s32',0x5c),
        'mPosY':('s32',0x60),'mPosZ':('s32',0x64),'mAngleY':('s16',0x8e),
        'mSpawnPosX':('s32',0x53c),'mSpawnPosY':('s32',0x540),
        'mSpawnPosZ':('s32',0x544),'mSpawnAngleY':('s16',0x698),
        'mGroundY':('s32',0x644),'mWaterSurfaceY':('s32',0x64c),
        'mToxicSurfaceY':('s32',0x650),'mOpacity':('u8',0x6f5),
        'mClpsWord1Lo':('s32',0x670),'mPrevAreaId':('u16',0x6a2),
        'mAreaId':('s8',0xcc),'mEggParams':('u8',0x704),
        'mLoadedResourceFlags':('u8',0x718),'mUseFarCamera':('u8',0x715),
        'mPeakY':('s32',0x684),
        'mAngle':('Vector3_16',0x8c),'mPrevAngle':('Vector3_16',0x92),
    }
    for name,(typ,offset) in fields.items():
        body=re.sub(r'\b'+name+r'\b','(*('+typ+' *)((char *)object+0x%x))'%offset,body)
    extra='struct Vector3 { s32 x,y,z; }; struct Vector3_16 { s16 x,y,z; }; struct StageMeshQuery { u8 bytes[0x1bc]; };\n'
    declarations='''
void func_ov002_020beabc(void *); void func_ov002_020c7dd0(void *,int);
void func_0205a588(void *,int,int);
'''
    row,bindings=projected(rel,body,declarations,['_ZN6Player13InitResourcesEv'],
                           extra=extra,auto_globals=False)
    for binding in bindings: binding['overlay']=2
    return row,bindings


def player_preload():
    rel='src/game/player/Player.cpp'
    body=function((ROOT/rel).read_text(),'void func_ov002_020bec2c(void)')
    body=body.replace('dExtFrameCtrl_c::LoadFile(*data_ov002_020ff480[',
                       '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(data_ov002_020ff480[')
    row,bindings=projected(rel,body,
        'char *_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(SharedFilePtr *);',
        ['func_ov002_020bec2c'],auto_globals=False)
    for binding in bindings: binding['overlay']=2
    return row,bindings


def player_entrance():
    rel='src/game/player/Player.cpp'
    raw=(ROOT/rel).read_text()
    body=function(raw,'int Player::St_LevelEnter_Init()')
    body=body.replace('int Player::St_LevelEnter_Init()',
                      'int _ZN6Player18St_LevelEnter_InitEv(void *object)')
    body=body.replace('this->SetAnim(', '_ZN6Player7SetAnimEjiit(object,')
    body=body.replace('this->GetBodyModelID(', '_ZNK6Player14GetBodyModelIDEjb(object,')
    body=body.replace('func_ov002_020c7cbc()', 'func_ov002_020c7cbc(object)')
    model='((void **)((char *)object+0xdc))'
    body=body.replace('mModels.mBodyModels[',model+'[')
    body=body.replace(model+'[_ZNK6Player14GetBodyModelIDEjb(object,(u8)param1, 0)]->SetFlags(0x40000000)',
                      '_ZN9Animation8SetFlagsEi((char *)'+model+'[_ZNK6Player14GetBodyModelIDEjb(object,(u8)param1, 0)]+0x50,0x40000000)')
    body=body.replace('&(dExtFrameCtrl_c &)*'+model+'[_ZNK6Player14GetBodyModelIDEjb(object,(u8)param1, 0)]',
                      '(StageFrame *)((char *)'+model+'[_ZNK6Player14GetBodyModelIDEjb(object,(u8)param1, 0)]+0x50)')
    body=body.replace('dExtFrameCtrl_c *p','StageFrame *p').replace('((char*)this)','object')
    fields={
        'mStateStep':('u8',0x6e3),'mKeyModelId':('s8',0x719),
        'mJumpComboStage':('u8',0x6e1),'mHorzSpeed':('s32',0x98),
        'mStateArg':('u8',0x70c),'mPrevAngleY':('s16',0x94),
        'mAngleY':('s16',0x8e),'mStateWork':('u8',0x6e5),
        'mPosX':('s32',0x5c),'mPosY':('s32',0x60),'mPosZ':('s32',0x64),
        'mStateWaitTimer':('u16',0x6a6),'mStateTimer':('u16',0x6a4),
        'mVertAccel':('s32',0x9c),'mVertSpeed':('s32',0xa8),
        'mIsUnderwater':('u8',0x706),'mIsAirborne':('u8',0x6de),
        'mLandSoundPlayed':('u8',0x6df),'param1':('u32',8),
    }
    for name,(typ,offset) in fields.items():
        body=re.sub(r'\b'+name+r'\b','(*('+typ+' *)((char *)object+0x%x))'%offset,body)
    extra='''
#include "nitro/fx/fx.h"
struct Vector3 { s32 x,y,z; };
struct StageFrame { u32 vtable,flags; s32 currFrame,speed; };
'''
    declarations='''
void _ZN6Player7SetAnimEjiit(void *,u32,int,int,u16);
u32 _ZNK6Player14GetBodyModelIDEjb(void *,u32,int);
void _ZN9Animation8SetFlagsEi(void *,int);
int func_ov002_020c7cbc(void *);
'''
    row,bindings=projected(rel,body,declarations,['_ZN6Player18St_LevelEnter_InitEv'],extra=extra,auto_globals=False)
    for binding in bindings: binding['overlay']=2
    return row,bindings


def player_set_animation():
    rel='src/game/player/Player.cpp'
    body=function((ROOT/rel).read_text(),'void Player::SetAnim(u32 animId, int flags, Fix12i speed, u16 startFrame)')
    body=body.replace('void Player::SetAnim(', 'void _ZN6Player7SetAnimEjiit(void *object,')
    body=body.replace('(char *)this','(char *)object').replace('Player *player','char *player').replace('(Player *)self','self')
    body=body.replace('void *, u32 a, int b','void *, unsigned int a, int b')
    body=body.replace('LAUNDER16(', '(')
    body=body.replace('data_ov002_020ff480[player->mCharFileBase + (player->param1 & 3)]->Release()',
                      '_ZN13SharedFilePtr7ReleaseEv(data_ov002_020ff480[player->mCharFileBase + (player->param1 & 3)])')
    body=body.replace('data_ov002_020ff480[player->mCharFileBase + charId]->Release()',
                      '_ZN13SharedFilePtr7ReleaseEv(data_ov002_020ff480[player->mCharFileBase + charId])')
    body=body.replace('dExtFrameCtrl_c::LoadFile(*', '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(')
    model='((void **)(player+0xdc))[id]'
    body=body.replace('player->mModels.mBodyModels[id]',model)
    body=body.replace('((dExtAnmModel_c *)modelPtr)->file','(*(void **)((char *)modelPtr+0x60))')
    body=body.replace(model+'->SetAnim(', '_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit('+model+',')
    body=body.replace('player->mModels.mTexSeqBody[field8].SetFile(*(BTP_File *)data_ov002_020ff2f0[sl]->filePtr,',
                      '_ZN15TextureSequence7SetFileER8BTP_Filei5Fix12IiEj(player+0x1dc+field8*0x14,data_ov002_020ff2f0[sl]->filePtr,')
    body=body.replace('(BCA_File *)','')
    for name,(typ,offset) in {
        'mStateFlags':('u16',0x6ce),'mCharFileBase':('u32',0x63c),
        'param1':('u32',8),'mCapFlags':('u16',0x73c),'mCapTimer':('u16',0x73e),
        'mHatCharacter':('u8',0x6dd),'mPrevCharacter':('u8',0x6dc),
    }.items():
        body=body.replace('player->'+name,'(*('+typ+' *)(player+0x%x))'%offset)
    declarations='''
void _ZN13SharedFilePtr7ReleaseEv(SharedFilePtr *);
void *_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit(void *,void *,int,int,u16);
void _ZN15TextureSequence7SetFileER8BTP_Filei5Fix12IiEj(void *,void *,int,int,u16);
'''
    row,bindings=projected(rel,body,declarations,['_ZN6Player7SetAnimEjiit'],auto_globals=False)
    for binding in bindings: binding['overlay']=2
    return row,bindings


def ground_scan():
    rel='src/engine/collision/d_bg_ch_sph_crr_query.cpp'
    body=function((ROOT/rel).read_text(),'int dBgCh_Gnd::DetectClsn()')
    body=body.replace('int dBgCh_Gnd::DetectClsn()',
                      'int _ZN9dBgCh_Gnd10DetectClsnEv(void *object)')
    body=body.replace('IsOwnedBy(', '_ZNK5dBgCh9IsOwnedByEP8dActor_c(object,')
    body=body.replace('dBgPi::SetCollider(', '_ZN5dBgPi11SetColliderEiiP8dActor_cP4dBgW((char *)object+0x10,')
    for name in ('o','obj'):
        body=body.replace(name+'->DetectClsn(*this)',name+'->vtable[6]('+name+',object)')
    body=body.replace('(Vector3*)((long long)(int)&this->pos)', '(Vector3 *)((char *)object+0x38)')
    body=re.sub(r'\bthis\b','object',body)
    for field,typ,off in [('mFlags','u32',0xb0),('mClipRadius','s32',0xb8),('mClipOffsetY','s32',0xb4)]:
        body=body.replace('p->'+field,'(*('+typ+' *)((char *)p+0x%x))'%off)
    extra='''
struct Vector3 { s32 x,y,z; }; struct Vec3 { int x,y,z; };
struct dActor_c;
struct dBgW { int (**vtable)(dBgW *,void *); };
extern dBgW *data_020a0c80[24];
'''
    declarations='''
void func_02037464(void *);
void *func_020393b4(void *); int func_020393ac(void *);
int func_0203939c(void *); int func_0203938c(void *);
bool _ZNK5dBgCh9IsOwnedByEP8dActor_c(void *,void *);
void _ZN5dBgPi11SetColliderEiiP8dActor_cP4dBgW(void *,int,int,void *,void *);
int Vec3_HorzDist(void *,void *);
'''
    return projected(rel,body,declarations,['_ZN9dBgCh_Gnd10DetectClsnEv'],extra=extra,auto_globals=False)


def line_scan():
    rel='src/engine/collision/d_bg_ch_sph_crr_query.cpp'
    body=function((ROOT/rel).read_text(),'bool dBgCh_Lin::DetectClsn()')
    body=body.replace('bool dBgCh_Lin::DetectClsn()',
                      'bool _ZN9dBgCh_Lin10DetectClsnEv(void *object)')
    body=body.replace('IsOwnedBy(', '_ZNK5dBgCh9IsOwnedByEP8dActor_c(object,')
    body=body.replace('dBgPi::SetCollider(', '_ZN5dBgPi11SetColliderEiiP8dActor_cP4dBgW((char *)object+0x10,')
    body=body.replace('((dBgW *)e)->DetectClsn(*this)', '((dBgW *)e)->vtable[7]((dBgW *)e,object)')
    body=body.replace('o->DetectClsn(*this)','o->vtable[7](o,object)')
    body=body.replace('dM3dGSph &sphere = mBoundSphere;',
                      'StageSphere *sphere = (StageSphere *)((char *)object+0x64);')
    body=body.replace('sphere.', 'sphere->')
    body=re.sub(r'\bthis\b','object',body)
    for field,typ,off in [('mFlags','u32',0xb0),('mClipRadius','s32',0xb8),('mClipOffsetY','s32',0xb4)]:
        body=body.replace('info->'+field,'(*('+typ+' *)((char *)info+0x%x))'%off)
    extra='''
struct Vector3 { s32 x,y,z; }; struct Vec3 { int x,y,z; };
struct StageSphere { u32 vtable; Vector3 centre; s32 radius; };
struct dActor_c; struct dBgW { int (**vtable)(dBgW *,void *); };
extern dBgW *data_020a0c80[24];
'''
    declarations='''
void *func_020393b4(void *); int func_020393ac(void *);
int func_0203939c(void *); int func_0203938c(void *);
bool _ZNK5dBgCh9IsOwnedByEP8dActor_c(void *,void *);
void _ZN5dBgPi11SetColliderEiiP8dActor_cP4dBgW(void *,int,int,void *,void *);
int Vec3_Dist(void *,void *);
'''
    return projected(rel,body,declarations,['_ZN9dBgCh_Lin10DetectClsnEv'],extra=extra,auto_globals=False)


def camera_init():
    rel='src/engine/view/d_camera.cpp'
    raw=(ROOT/rel).read_text()
    body=function(raw,'int dCamera_c::InitResources()')
    body=body.replace('int dCamera_c::InitResources()', 'int _ZN9dCamera_c13InitResourcesEv(void *object)')
    body=body.replace('dCamera_c *thiz = this','char *thiz = (char *)object')
    body=body.replace('::operator new(', '_Znwj(')
    body=body.replace('((dActor_c *)thiz->mTargetPlayer)->mAreaId',
                      '(*(s8 *)((char *)thiz->mTargetPlayer+0xcc))')
    body=body.replace('ChangeState((State *)&data_0209b008)',
                      '_ZN9dCamera_c11ChangeStateEPNS_5StateE(object,&data_0209b008)')
    body=body.replace('return this->Render()', 'return _ZN9dCamera_c6RenderEv(object)')
    body=body.replace('func_0200d0ac((int)thiz,', 'func_0200d0ac(thiz,')
    fields={'mFixedViewPos':('void *',0x148),'mFovBlend':('s32',0x168),
            'mTargetPlayer':('u32',0x110),'mFramedActor':('u32',0x114),'mFocusActor':('u32',0x118)}
    for name,(typ,off) in fields.items():
        body=body.replace('thiz->'+name,'(*('+typ+' *)(thiz+0x%x))'%off)
    extra='typedef struct CamViewObj { u8 type,p1; s16 x,y,z; } CamViewObj;\n'
    declarations='''
extern u8 data_0209f1f8; extern u8 data_0209f250;
extern void *data_0209f394[4]; extern u32 data_0209b008;
CamViewObj *GetViewObj(int); void *_Znwj(u32);
void ChangeArea(int); int StartWithFarCamera(void);
int _ZN9dCamera_c11ChangeStateEPNS_5StateE(void *,void *);
void func_0200cf40(void *); void func_0200d0ac(void *,u8);
int _ZN9dCamera_c6RenderEv(void *);
'''
    return projected(rel,body,declarations,['_ZN9dCamera_c13InitResourcesEv'],extra=extra,auto_globals=False)


def camera_render():
    rel='src/engine/view/d_camera.cpp'
    body=function((ROOT/rel).read_text(),'int dCamera_c::Render()')
    body=body.replace('int dCamera_c::Render()', 'int _ZN9dCamera_c6RenderEv(void *object)')
    body=body.replace('dCamera_c *self = this', 'char *self = (char *)object')
    for name,symbol in {
        'OAM::Render':'_ZN3OAM6RenderEbP7OamAttriiii5Fix12IiES3_ii',
        'G2x::SetBlendAlpha':'_ZN3G2x13SetBlendAlphaEPVttttj',
        'G3i::LookAt_':'_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3',
        'dView_c::Render()':'_ZN7dView_c6RenderEv(object)',
    }.items(): body=body.replace(name,symbol)
    body=body.replace('self->mViewport','((u8 *)(self+0x10c))')
    fields={
        'lookAt':('Vector3',0x80),'pos':('Vector3',0x8c),
        'mPrevLookAt':('Vector3',0xc8),'mPrevPos':('Vector3',0xd4),
        'mFovAngle':('s16',0x17a),'mAngleZ':('s16',0x178),
        'mFlags':('u32',0x154),'mMode':('void *',0x13c),'mFovBlend':('s32',0x168),
        'mShakeY':('s32',0x134),'mRollShake':('s16',0x18c),'mRollShakePhase':('s16',0x18a),
        'mFovShake':('s16',0x18e),'mFovShakePhase':('s16',0x192),
        'mAspect':('s32',0xf8),'mNear':('s32',0xfc),'mFar':('s32',0x100),
        'viewMat':('Matrix4x3',0x50),
    }
    for name,(typ,off) in fields.items():
        body=re.sub(r'\bself->'+name+r'\b','(*('+typ+' *)(self+0x%x))'%off,body)
    extra='''
#include "nitro/fx/fx.h"
struct Vector3 { s32 x,y,z; }; struct Vec3 { s32 x,y,z; }; struct OamAttr;
'''
    declarations='''
int Vec3_Dist(Vector3 *,Vector3 *); int Vec3_HorzDist(Vector3 *,Vector3 *);
void func_0200af20(void *,Vec3 *,Vec3 *,s16 *); void func_0200d954(void *,s16);
void _ZN3OAM6RenderEbP7OamAttriiii5Fix12IiES3_ii(bool,OamAttr *,int,int,int,int,int,int,int,int);
void _ZN3G2x13SetBlendAlphaEPVttttj(volatile u16 *,u16,u16,u16,u32);
void _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,bool,Matrix4x3 *);
void _ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(Vector3 *,Vector3 *,Vector3 *,bool,Matrix4x3 *);
void Matrix4x3_FromRotationZ(Matrix4x3 *,s16); void MulMat4x3Mat4x3(Matrix4x3 *,Matrix4x3 *,Matrix4x3 *);
int _ZN7dView_c6RenderEv(void *);
'''
    return projected(rel,body,declarations,['_ZN9dCamera_c6RenderEv'],extra=extra)


def view_render():
    rel='src/engine/view/d_view.cpp'
    body=function((ROOT/rel).read_text(),'s32 dView_c::Render()')
    body=body.replace('s32 dView_c::Render()', 's32 _ZN7dView_c6RenderEv(void *object)')
    body=body.replace('CopyToViewMat(&viewMat)', '_Z13CopyToViewMatPK9Matrix4x3((Matrix4x3 *)((char *)object+0x50))')
    return projected(rel,body,'void _Z13CopyToViewMatPK9Matrix4x3(Matrix4x3 *);',['_ZN7dView_c6RenderEv'])


def stage_areas():
    rel='src/game/stages/d_s_stage_methods.cpp'
    raw=(ROOT/rel).read_text()
    names=('HideArea','ShowArea','ChangeArea')
    body='\n'.join(function(raw,'extern "C" void '+name+'(') for name in names)
    declarations='void HideArea(int); void ShowArea(int);\n'
    return projected(rel,body,declarations,list(names))


def clipper_frustum():
    rel='src/engine/collision/d_clipper.cpp'
    raw=(ROOT/rel).read_text()
    build=function(raw,'void dClipper::Func_0201559C()')
    build=build.replace('void dClipper::Func_0201559C()',
                        'void _ZN8dClipper13Func_0201559CEv(void *object)')
    setup=function(raw,'void dClipper::Func_020156DC(Fix12i aspectRatio, u16 fovAngle, Fix12i nearZ, Fix12i farZ)')
    setup=setup.replace('void dClipper::Func_020156DC(',
                        'void _ZN8dClipper13Func_020156DCEitii(void *object,')
    setup=setup.replace('Func_0201559C()', '_ZN8dClipper13Func_0201559CEv(object)')
    body=build+'\n'+setup
    body=body.replace('mPlaneNormals','((Vector3 *)((char *)object+4))')
    for name,(typ,off) in {'mFovAngle':('u16',0x58),'mNearZ':('s32',0x50),
                            'mFarZ':('s32',0x54),'mAspectRatio':('s32',0x4c)}.items():
        body=re.sub(r'\b'+name+r'\b','(*('+typ+' *)((char *)object+0x%x))'%off,body)
    extra='#include "nitro/fx/fx.h"\nstruct Vector3 { s32 x,y,z; };\n'
    declarations='''
extern s16 data_02082214[];
int _ZN4cstd4fdivEii(int,int);
void CrossVec3(Vector3 *,Vector3 *,Vector3 *); void NormalizeVec3(Vector3 *,Vector3 *);
'''
    return projected(rel,body,declarations,['_ZN8dClipper13Func_0201559CEv',
                 '_ZN8dClipper13Func_020156DCEitii'],extra=extra,auto_globals=False)


def ground_kcl(line=False):
    rel='src/engine/collision/d_bg_w_kc_itcm.cpp'
    symbol='_ZN7dBgW_Kc10DetectClsnER9dBgCh_'+('Lin' if line else 'Gnd')
    signature=('s32 dBgW_Kc::DetectClsn(dBgCh_Lin &ray)' if line else
               'int dBgW_Kc::DetectClsn(dBgCh_Gnd &ray)')
    body=function((ROOT/rel).read_text(),signature)
    body=body.replace(signature,'int '+symbol+'(void *object,'+('Line' if line else 'Ground')+' *ray)')
    body=body.replace('this->kclFile','(*(KCL_File **)((char *)object+0x20))')
    body=body.replace('kclFile','(*(KCL_File **)((char *)object+0x20))')
    body=body.replace('ray.','ray->').replace('GetSurfaceInfo(', '_ZN7dBgW_Kc14GetSurfaceInfoEsR11SurfaceInfo(object,')
    body=body.replace('dBgCh::ShouldPassThroughImpl(', '_ZN5dBgCh21ShouldPassThroughImplEPvRK4CLPSRKS_b(')
    body=body.replace('data_020a0cec.clps','&data_020a0cec.clps')
    body=body.replace(', data_020a0cec)',', &data_020a0cec)')
    body=body.replace('((dBgPi &)ray).RecordHit(', '_ZN5dBgPi9RecordHitEsP11SurfaceInfo((char *)ray+0x10,')
    if line:
        body=body.replace('GetSurfaceInfo(', '_ZN7dBgW_Kc14GetSurfaceInfoEsR11SurfaceInfo(object,') if 'GetSurfaceInfo(' in body else body
        body=body.replace(', info)',', &info)').replace('info.clps','&info.clps')
        body=body.replace('info.CopyNormalTo(normal)', 'normal=info.normal')
        body=body.replace('ray->SetClsnPos(pos)','ray->lineEnd=pos')
        body=body.replace('(dBgPc *)&info','(void *)&info')
    body=re.sub(r'\bthis\b','object',body).replace('cstd::fdiv','_ZN4cstd4fdivEii')
    # Lift's 32-bit cast helper is named word; keep the octree's local distinct.
    body=re.sub(r'\bword\b','octreeWord',body)
    header=(ROOT/'include/dBgW_Kc.h').read_text()
    records='\n'.join(re.search(r'struct '+name+r' \{.*?\n\};',header,re.S).group() for name in ('KCL_Tri','KCL_File'))
    extra='''
struct Vector3 { s32 x,y,z; };
struct CLPS { u32 w0,w1; };
struct SurfaceInfo { CLPS clps; Vector3 normal; };
struct Ground { u8 bases[0x38]; Vector3 pos; s32 clsnY; u8 hasClsn,pad[3]; s32 height; };
struct Line { u8 bases[0x38]; Vector3 start,end; u8 hasClsn,pad[3]; Vector3 lineEnd; s32 clsnDist; u8 sphere[0x14]; };
static_assert(sizeof(Ground)==0x50);
static_assert(sizeof(Line)==0x78);
extern SurfaceInfo data_020a0cec;
'''+records+'''\n#define REL_DOT(n) ((n)[0]*rel.x+(n)[1]*rel.y+(n)[2]*rel.z)\n'''
    declarations='''
int _ZN4cstd4fdivEii(int,int); bool func_020397dc(int);
s16 func_020396dc(void *,KCL_Tri *);
void _ZN7dBgW_Kc14GetSurfaceInfoEsR11SurfaceInfo(void *,s16,SurfaceInfo *);
bool _ZN5dBgCh21ShouldPassThroughImplEPvRK4CLPSRKS_b(void *,CLPS *,void *,bool);
void _ZN5dBgPi9RecordHitEsP11SurfaceInfo(void *,s16,SurfaceInfo *);
'''
    if line:
        declarations+='''
void _ZN5dBgPcC1Ev(void *); void _ZN5dBgPcD1Ev(void *);
bool func_020397b8(int); int Vec3_Dist(Vector3 *,Vector3 *);
'''
    return projected(rel,body,declarations,[symbol],extra=extra,auto_globals=False)


def sphere_kcl():
    # Complete dBgW_Kc::DetectClsn(dBgCh_SphCrr &) from the recovered ITCM
    # source, with its macros and raw-sqrt helpers. Members become their
    # header offsets: dBgW_Kc (kclFile 0x20, upAxis 0x28, shallowOnly 0x34,
    # depthByDir 0x35, depthDir 0x38, depthDirMinDot 0x44, edgeToleranceShift
    # 0x48, edgeContacts 0x4c, tolerantWallEdges 0x4d) and dBgCh_SphCrr
    # (dBgPi base 0x10, centre 0x3c, radius 0x48, flags 0x70,
    # mWallRejectHalfExtent 0xec, mFloorNormal 0xfc, mMaxNormalY 0x108).
    rel='src/engine/collision/d_bg_w_kc_itcm.cpp'
    raw=(ROOT/rel).read_text()
    symbol='_ZN7dBgW_Kc10DetectClsnER12dBgCh_SphCrr'
    signature='s32 dBgW_Kc::DetectClsn(dBgCh_SphCrr &sphere)'
    start=raw.index('#define SQRT_RESULT')
    end=raw.index('/* ---- defs in reverse ROM order ---- */')
    helpers=raw[start:end]
    body=function(raw,signature)
    body=body.replace(signature,'int '+symbol+'(void *object,char *sphere)')
    text=helpers+'\n'+body
    reps=[
        ('((const dBgCh_SphCrr &)sphere).flags','(*(u8 *)(sphere+0x70))'),
        ('((dBgPi &)sphere).RecordHit(','_ZN5dBgPi9RecordHitEsP11SurfaceInfo(sphere+0x10,'),
        ('sphere.RecordFloorHit(','func_020379f4(sphere,'),
        ('sphere.RecordWallHit(','func_020379c0(sphere,'),
        ('sphere.RecordUnderHit(','func_0203798c(sphere,'),
        ('sphere.SetFloorNormal(','func_0203794c((int *)sphere,(int *)'),
        ('sphere.ExpandPushOutBounds(','func_02037a6c(sphere,'),
        ('sphere.flags','(*(u8 *)(sphere+0x70))'),
        ('sphere.radius','(*(s32 *)(sphere+0x48))'),
        ('sphere.centre','(*(Vector3 *)(sphere+0x3c))'),
        ('sphere.pos','(*(Vector3 *)(sphere+0x3c))'),
        ('sphere.mWallRejectHalfExtent','(*(s32 *)(sphere+0xec))'),
        ('sphere.mFloorNormal','(*(Vector3 *)(sphere+0xfc))'),
        ('sphere.mMaxNormalY','(*(s32 *)(sphere+0x108))'),
        ('data_020a0cec.clps.GetFloorClass()','func_02037e58((unsigned int *)&data_020a0cec.clps)'),
        ('data_020a0cec.CopyNormalTo(sn)','sn=data_020a0cec.normal'),
        ('dBgCh::ShouldPassThroughImpl(this, data_020a0cec.clps, sphere,',
         '_ZN5dBgCh21ShouldPassThroughImplEPvRK4CLPSRKS_b(object, &data_020a0cec.clps, sphere,'),
        ('GetSurfaceInfo(triID, data_020a0cec)','_ZN7dBgW_Kc14GetSurfaceInfoEsR11SurfaceInfo(object,triID,&data_020a0cec)'),
        ('cstd::fdiv','_ZN4cstd4fdivEii'),
    ]
    for old,new_ in reps: text=text.replace(old,new_)
    members={'kclFile':'(*(KCL_File **)((char *)object+0x20))',
             'upAxis':'(*(PlainVec *)((char *)object+0x28))',
             'shallowOnly':'(*(u8 *)((char *)object+0x34))',
             'depthByDir':'(*(u8 *)((char *)object+0x35))',
             'depthDirMinDot':'(*(s32 *)((char *)object+0x44))',
             'depthDir':'(*(PlainVec *)((char *)object+0x38))',
             'edgeToleranceShift':'(*(s32 *)((char *)object+0x48))',
             'edgeContacts':'(*(u8 *)((char *)object+0x4c))',
             'tolerantWallEdges':'(*(u8 *)((char *)object+0x4d))'}
    for name,expr in members.items():
        text=re.sub(r'(?<![.>\w])\b'+name+r'\b',expr,text)
    # The lift takes no function-like macros: expand the unit's own macros
    # with the C preprocessor (comments and line markers dropped).
    import subprocess
    fx=re.search(r'#define FX_MUL\([^\n]*',(ROOT/'include/nitro/fx/fx.h').read_text())[0]
    text=subprocess.run(['gcc','-E','-P','-x','c++','-'],input=fx+'\n'+text,
                        capture_output=True,text=True,check=True).stdout
    text=re.sub(r'\bthis\b','object',text)
    text=re.sub(r'\bword\b','octreeWord',text)
    # A volatile-qualified value cast only pins the ROM's register round
    # trip; on the host it is the same value.
    text=text.replace('(volatile s32)','(s32)')
    header=(ROOT/'include/dBgW_Kc.h').read_text()
    records='\n'.join(re.search(r'struct '+name+r' \{.*?\n\};',header,re.S).group() for name in ('KCL_Tri','KCL_File'))
    extra='''#include "nitro/fx/fx.h"
struct Vector3 { s32 x,y,z; };
struct Vector3s { s16 x,y,z; };
struct PlainVec { s32 x,y,z; };
struct CLPS { u32 w0,w1; };
struct SurfaceInfo { CLPS clps; Vector3 normal; };
extern SurfaceInfo data_020a0cec;
'''+records+'\n'
    declarations='''
int _ZN4cstd4fdivEii(int,int); bool func_020397dc(int); int func_02039794(int);
s16 func_020396dc(void *,KCL_Tri *);
void _ZN7dBgW_Kc14GetSurfaceInfoEsR11SurfaceInfo(void *,s16,SurfaceInfo *);
bool _ZN5dBgCh21ShouldPassThroughImplEPvRK4CLPSRKS_b(void *,CLPS *,void *,bool);
void _ZN5dBgPi9RecordHitEsP11SurfaceInfo(void *,s16,SurfaceInfo *);
s32 DotVec3(const s32 *,const Vector3 *);
void func_020379f4(char *,int,void *); void func_020379c0(char *,int,void *);
void func_0203798c(char *,int,void *); void func_0203794c(int *,int *);
void func_02037a6c(char *,s32,s32,s32,s32,s32,s32); int func_02037e58(unsigned int *);
'''
    return projected(rel,text,declarations,[symbol],extra=extra,auto_globals=False)


def arm9_initializers():
    # crt0 runs the main module's static-initializer table (.ctors at
    # 0x02086b60) before main. The recovered bodies patch script pointers,
    # build the global faders/wipe/clipper and register their destructors;
    # destructor arguments are original code handles, never host pointers.
    imports={'func_020731dc':'void func_020731dc(void *,void *,void *);'}
    destructors=('_ZN10dFdColor_cD1Ev','_ZN15dFdBrightness_cD1Ev','func_0201973c',
                 'func_0201aaec','_ZN8dClipperD1Ev','_ZN7dWipe_cD1Ev','dBgPc_Destroy',
                 '_ZN9dBgCh_LinD1Ev','NoOpDestructor','_ZN7Vector3D1Ev',
                 'NullDestructor_0203d47c','_ZN8Vector3sD1Ev')
    # dBgPc.h spells the dBgPc destructor through a C alias.
    SYMBOLS.setdefault('dBgPc_Destroy',SYMBOLS['_ZN5dBgPcD1Ev'])
    for path in sorted((ROOT/'src/unnamed/arm9/0207').glob('__sinit_*')):
        rel=str(path.relative_to(ROOT)); name=path.stem
        if name=='__sinit_02075154':
            continue  # already linked once by the minigame manifest; bound in stage_runtime
        raw=re.sub(r'^#include[^\n]*','',path.read_text(),flags=re.M)
        raw=re.sub(r'^//cpp\n','',raw,flags=re.M)
        if name=='__sinit_02074dc4':
            # Timer.h is not imported; the timer record is passed by address.
            raw=raw.replace('extern struct Timer data_0209d4c8;','extern char data_0209d4c8[];')
            raw=raw.replace('extern struct Timer *func_0201964c(struct Timer *timer);',
                            'extern void *func_0201964c(void *timer);')
            raw=raw.replace('func_0201964c(&data_0209d4c8);','func_0201964c(data_0209d4c8);')
        if name=='__sinit_02074e0c':
            # The ABI of func_0201aa18 (external/sm64ds-pc/src/func_0201aa18.c).
            raw=raw.replace('extern void func_0201aa18();','extern void *func_0201aa18(char *c);')
            raw=raw.replace('extern int data_0209d574[];','extern char data_0209d574[];')
        if name=='__sinit_02074fe4':
            # dBgPc.h's C aliases for the class constructor; the BSS record
            # is passed through as an opaque address.
            raw=raw.replace('extern struct dBgPc data_020a0cec[];',
                            'extern char data_020a0cec[];\nextern void *_ZN5dBgPcC1Ev(void *);')
            raw=raw.replace('dBgPc_Construct(','_ZN5dBgPcC1Ev(')
        raw=raw.replace('extern "C" void '+name+'(void);\n','')
        raw=raw.replace('extern "C" ','extern ')
        for imported,declaration in imports.items():
            raw=re.sub(r'^extern [^;\n]*?\b'+imported+r'\([^;]*;',declaration,raw,flags=re.M)
        for destructor in destructors:
            raw=re.sub(r'^extern [^;\n]*?\b'+destructor+r'(?:\([^;]*|\[\]);','',raw,flags=re.M)
            if re.search(r'\b'+destructor+r'\s*\(',raw):
                raise ValueError('initializer invokes destructor: '+rel)
            raw=re.sub(r'&\s*\b'+destructor+r'\b','((void *)0x%08xu)'%SYMBOLS[destructor],raw)
            raw=re.sub(r'\b'+destructor+r'\b','((void *)0x%08xu)'%SYMBOLS[destructor],raw)
        row,bindings=projected(rel,raw,'',[name],auto_globals=False)
        yield row,bindings


def overlay_initializers():
    # The pinned dependency retains complete matching C bodies for initializers
    # which newer class TUs express as implicit C++ static construction. Keep
    # those bodies and their module ownership; do not copy initialized ROM data
    # over the BSS or invoke another overlay's code at a colliding DS address.
    modules=(2,9,62,69,85,89,94,98,100,102)
    sources=[]
    for module in modules:
        path='external/sm64ds-pc/config/arm9/overlays/ov%03d/symbols.txt'%module
        for line in (ROOT/path).read_text().splitlines():
            match=re.match(r'(\S+) kind:(\w+).*?addr:0x([0-9a-f]+)',line)
            if not match: continue
            name,kind,address=match[1],match[2],int(match[3],16)
            if kind!='function':
                STAGE_DATA[name]=address
                DATA_SYMBOLS[name]=address
            elif name.startswith('__sinit_'):
                if name=='__sinit_d_a_star_gate.cpp':
                    if module!=100 or address!=0x02147a70:
                        raise ValueError('star-gate initializer address changed')
                    name='__sinit_ov100_02147a70'
                    SYMBOLS[name]=address
                    sources.append((module,'android/compat/ov100_star_gate_init.c',name))
                    continue
                SYMBOLS[name]=address
                sources.append((module,'external/sm64ds-pc/src/'+name+'.c',name))
    imports={name:'void *'+name+'(void *,u32);' for name in (
        'func_02017acc','func_02017b4c','_ZN13SharedFilePtr9ConstructEj',
        'SharedFilePtr_Construct_TexSeq')}
    imports.update({'func_020731dc':'void func_020731dc(void *,void *,void *);',
                    'func_0203b9b4':'void func_0203b9b4(int *,int);',
                    '_ZN8SaveData22NumGlowingRabbitsFoundEv':'int _ZN8SaveData22NumGlowingRabbitsFoundEv(void);'})
    destructors=('func_02017ab4','SharedFilePtr_Destruct_Anim','SharedFilePtr_Destruct_Clsn',
                 'SharedFilePtr_Destruct_TexSeq','_ZN7Vector3D1Ev','func_0201973c')
    for module,rel,name in sources:
        raw=re.sub(r'^#include[^\n]*','',(ROOT/rel).read_text(),flags=re.M)
        if name=='__sinit_ov002_021019d0':
            # The matching C shard predates the helper's explicit declaration.
            raw='void *func_ov002_020e6ad4(void *);\n'+raw
            SYMBOLS['func_ov002_020e6ad4']=0x020e6ad4
        for imported,declaration in imports.items():
            raw=re.sub(r'^extern [^;\n]*?\b'+imported+r'\([^;]*;',declaration,raw,flags=re.M)
        # These references register destructor *code handles*. They are not
        # data arrays, narrowed host pointers or calls during initialization.
        for destructor in destructors:
            raw=re.sub(r'^extern [^;\n]*?\b'+destructor+r'(?:\([^;]*|\[\]);','',raw,flags=re.M)
            if re.search(r'\b'+destructor+r'\s*\(',raw):
                raise ValueError('initializer invokes destructor: '+rel)
            raw=re.sub(r'&\s*\b'+destructor+r'\b','((void *)0x%08xu)'%SYMBOLS[destructor],raw)
            raw=re.sub(r'\b'+destructor+r'\b','((void *)0x%08xu)'%SYMBOLS[destructor],raw)
        row,bindings=projected(rel,raw,'',[name],auto_globals=False)
        for binding in bindings: binding['overlay']=module
        yield row,bindings
    rel='src/unnamed/ov002/func_ov002_020e6ad4.c'
    row,bindings=free_c(rel)
    for binding in bindings: binding['overlay']=2
    yield row,bindings


def stage():
    rel = 'src/game/stages/d_s_stage_methods.cpp'
    raw = (ROOT/rel).read_text()
    body = function(raw, 'int dScStage_c::InitResources() {')
    table_setters=['func_0202b0e0','func_0202b0c4','func_0202b090',
                   'func_0202b060','func_0202b044','func_0202af80']
    table_bodies='\n'.join(function(raw,'extern "C" void '+name+'(int a, int b)')
                           for name in table_setters)
    table_bodies+='\n'+function(raw,'extern "C" u32 DeathTable_GetBit(s32 id)')
    body = replace_once(body, 'int dScStage_c::InitResources()', 'int _ZN10dScStage_c13InitResourcesEv(u32 actor)')
    body = replace_once(body, 'dScStage_c *scene = this;', 'Stage *scene = (Stage *)actor;')
    # The complete controller reset uses its shared, pointer-free 0x18 record.
    controls = re.search(r'struct CamInput \{.*?\n\};',raw,re.S).group()
    reset = function(raw,'extern "C" void ResetInput(void) {')
    sound_group = function(raw,'extern "C" int GetSoundGroupID(int group, int sub) {')
    graphics=function(raw,'void dScStage_c::LoadGraphics2D(bool vsMode, int sublevel) {')
    graphics=replace_once(graphics,'void dScStage_c::LoadGraphics2D(bool vsMode, int sublevel)',
                          'void _ZN10dScStage_c14LoadGraphics2DEbi(bool vsMode, int sublevel)')
    for name in ('GetBG0CharPtr','GetBG0ScrPtr','GetBG1ScrPtr','GetBG2ScrPtr'):
        graphics=graphics.replace('G2S::'+name,'_ZN3G2S'+str(len(name))+name+'Ev')
    graphics_enum=re.search(r'enum ZN10dScStage_c14LoadGraphics2DEbi_AssetId \{.*?\};',raw,re.S)[0]
    graphics_macros='\n'.join(line for line in raw.splitlines() if re.match(r'#define BG[0-3]CNT',line))
    overlay_symbols = {
        'LoadLevelOverlays':'_Z17LoadLevelOverlaysi',
        'UnloadLevelOverlays':'_Z19UnloadLevelOverlaysi',
        'LoadOrUnloadObjectOverlays':'_Z26LoadOrUnloadObjectOverlaysPFviEi',
    }
    overlays = '\n'.join(function(raw,signature) for signature in (
        'void LoadLevelOverlays(int level) {',
        'void UnloadLevelOverlays(int levelID) {',
        'void LoadOrUnloadObjectOverlays(void (*fn)(int), int idx) {',
        'extern "C" void func_0202deac(int levelID) {'))
    for name,symbol in sorted(overlay_symbols.items(),key=lambda item:len(item[0]),reverse=True):
        overlays = re.sub(r'\b'+name+r'\b',symbol,overlays)
    # Absolute linker symbols denote overlay IDs, not pointers to host data.
    for overlay in (60,98,102):
        overlays = replace_once(overlays,'(int)&OVERLAY_'+str(overlay)+'_ID',str(overlay))
    substitutions = {
        'Sound::Player::SetPlayableSeqCount':'_ZN5Sound6Player19SetPlayableSeqCountEii',
        'dScene_c::Initialise3dGraphics':'_ZN8dScene_c20Initialise3dGraphicsEv',
        'dScStage_c::SetVramBanks':'_ZN10dScStage_c12SetVramBanksEv',
        'Sound::LoadGroupAndSetBank':'_ZN5Sound19LoadGroupAndSetBankEii',
        'Sound::LoadAndSetMusic_Layer1':'_ZN5Sound22LoadAndSetMusic_Layer1Ei',
        'Sound::SetMusic':'_ZN5Sound8SetMusicEjj',
        'dExtShadowModel_c::CleanAll':'_ZN17dExtShadowModel_c8CleanAllEv',
        'GX::DisableAllBanks':'_ZN2GX15DisableAllBanksEv',
        'GX::SetGraphicsMode':'_ZN2GX15SetGraphicsModeEiii',
        'GXS::SetGraphicsMode':'_ZN3GXS15SetGraphicsModeEi',
        'GX::DispOn':'_ZN2GX6DispOnEv',
        '::Initialise3dGraphics':'Initialise3dGraphics',
        'LoadLevelOverlays':'_Z17LoadLevelOverlaysi',
        'mHeap::Heap_t':'Heap',
    }
    for old,new in substitutions.items(): body = body.replace(old,new)
    body = replace_once(body, '((Heap *)data_020a0eac)->SetDefault()',
                        '_ZN5mHeap6Heap_t10SetDefaultEv((Heap *)data_020a0eac)')
    body = replace_once(body, 'previousHeap->SetDefault()', '_ZN5mHeap6Heap_t10SetDefaultEv(previousHeap)')
    body = replace_once(body, 'dExtSimpleModel_c::LoadFile(*(SharedFilePtr *)data_020756f0[i])',
                        '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr((SharedFilePtr *)data_020756f0[i])')
    body = replace_once(body, '((dFdWipe_c *)((char *)data_0209f324 + faderOffset))\n                ->LoadAndSetFile(faderFileIds[faderIndex])',
                        '_ZN9dFdWipe_c14LoadAndSetFileEt((Fader *)((char *)data_0209f324 + faderOffset), faderFileIds[faderIndex])')
    for method,symbol in [('LoadModel','_ZN10dScStage_c9LoadModelEv'),
                          ('LoadFog','_ZN10dScStage_c7LoadFogEv'),
                          ('LoadTextureTransformers','_ZN10dScStage_c23LoadTextureTransformersEv'),
                          ('LoadSkybox','_ZN10dScStage_c10LoadSkyboxEv')]:
        body = replace_once(body,'scene->'+method+'()',symbol+'(scene)')
    body = replace_once(body,'scene->mSysTracker.Initialise()',
                        '_ZN8Particle10SysTracker10InitialiseEv(&scene->mSysTracker)')
    body = replace_once(body,'dScStage_c::LoadClsnAndObjects(*(LVL_Overlay *)data_0209f340, data_0209f264,\n                                       scene->mMeshCollider)',
                        '_ZN10dScStage_c18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc((LVL_Overlay *)data_0209f340, data_0209f264, &scene->mMeshCollider)')
    # Array-reference spelling is otherwise kept verbatim by Lift.
    body = body.replace('*(s8 *)(unsigned)(((unsigned long long)(unsigned)&data_0209f2f8) & ~0ULL)',
                        '*(s8 *)&data_0209f2f8')
    body = body.replace('(s32)&VS_STAR_SPAWN_ORDERS', '(int)&VS_STAR_SPAWN_ORDERS')
    prototypes = raw[raw.index('void Enable3dEngines(void);'):raw.index('int dScStage_c::InitResources() {')]
    prototypes = prototypes[:prototypes.rfind('}')]
    prototypes = prototypes.replace('void *_ZN9dFdWipe_cC1Ev(void *thiz);',
                                    'void *_ZN9dFdWipe_cC1Ev(void *);')
    prototypes = re.sub(r'//[^\n]*','',prototypes)
    # Remove parameter names from the imported prototypes.
    prototypes = '''
void Enable3dEngines(void); void ResetInput(void); int SublevelToLevel(int);
int GetLevelPart(int);
void UnloadArchive(int); void LoadTextNarcs(void); int LoadArchive(int);
void Initialise3dGraphics(int); void InitialiseVramGlobals(void);
void func_02039218(void); void ResetKuppaScript(void);
int GetSoundGroupID(int,int); void LoadDebugFont(void);
int IsLevelInsideCastle(int); int IsLevelTinyHugeIslandOutside(int);
int func_020308a8(void); int func_0203da9c(void); int func_0203da3c(void);
u32 func_0203dad4(void); void func_0203b9b4(int *,int);
void *func_02073470(int,int,int,void *,void *); void func_ov001_020ab2e4(void);
void *_ZN9dFdWipe_cC1Ev(void *); void _ZN9dFdWipe_cD1Ev(void *);
void _ZN10dScStage_c14LoadGraphics2DEbi(bool,int);
void _ZN5Sound6Player19SetPlayableSeqCountEii(int,int);
void _ZN8dScene_c20Initialise3dGraphicsEv(void);
void _ZN10dScStage_c12SetVramBanksEv(void);
void _ZN5Sound19LoadGroupAndSetBankEii(int,int);
void _ZN5Sound22LoadAndSetMusic_Layer1Ei(int);
void _ZN5Sound8SetMusicEjj(u32,u32);
void _ZN17dExtShadowModel_c8CleanAllEv(void);
void _ZN2GX15DisableAllBanksEv(void);
void _ZN2GX15SetGraphicsModeEiii(int,int,int); void _ZN3GXS15SetGraphicsModeEi(int);
void _ZN2GX6DispOnEv(void); void _Z17LoadLevelOverlaysi(int);
Heap *_ZN5mHeap6Heap_t10SetDefaultEv(Heap *);
void *_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN9dFdWipe_c14LoadAndSetFileEt(Fader *,u16);
void _ZN10dScStage_c9LoadModelEv(Stage *); void _ZN10dScStage_c7LoadFogEv(Stage *);
void _ZN10dScStage_c23LoadTextureTransformersEv(Stage *);
void _ZN10dScStage_c10LoadSkyboxEv(Stage *);
void _ZN8Particle10SysTracker10InitialiseEv(SysTracker *);
void _ZN10dScStage_c18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc(LVL_Overlay *,u32,MeshCollider *);
u8 GetControllerMode(int); void func_0205a588(void *,int,int);
void Crash(void); void LoadOverlay(int); void UnloadOverlay(int);
void _Z26LoadOrUnloadObjectOverlaysPFviEi(void (*)(int),int);
void SetBg0Offset(int,int); void SetSubBg0Offset(int,int);
void SetSubBg1Offset(int,int); void SetSubBg2Offset(int,int);
void LoadFont(u8); int GetOwnerLanguage(void); void LoadFont3D(void);
u32 LoadCompressedFileAt(u16,void *); int LoadFileAt(int,int);
char *_ZN2G213GetBG2CharPtrEv(void); char *_ZN2G212GetBG2ScrPtrEv(void);
char *_ZN3G2S13GetBG0CharPtrEv(void); char *_ZN3G2S12GetBG0ScrPtrEv(void);
char *_ZN3G2S12GetBG1ScrPtrEv(void); char *_ZN3G2S12GetBG2ScrPtrEv(void);
void LoadControllerModeText(int); void MultiStore16(u16,char *,int);
'''
    return projected(rel,reset+'\n'+sound_group+'\n'+overlays+'\n'+graphics+'\n'+body+'\n'+table_bodies,prototypes,
                     ['ResetInput','_ZN10dScStage_c13InitResourcesEv',
                      *overlay_symbols.values(),'func_0202deac','GetSoundGroupID',
                      '_ZN10dScStage_c14LoadGraphics2DEbi',*table_setters,'DeathTable_GetBit'],
                     extra=controls+'\nstruct OvTbl { u32 t[7]; };\n'+graphics_enum+'\n'+graphics_macros+'\n')


if __name__ == '__main__':
    rows,bindings = [],[]
    for row,code in [stage(),sound(),narc(),scene_graphics(),graphics_3d(),controller_text(),controller_glyphs(),collision_reset(),cap_reset(),save_predicate(),shared_file_handles(),common_models()]:
        rows.append(row); bindings.extend(code)
    # Distinct execution slices of the same original TU retain its source hash.
    for unit,(row,code) in [('stage_model_instances',model_instances()),
                            ('stage_level_model',stage_model()),
                            ('stage_level_objects',level_objects()),
                            ('stage_player_init',player_init()),
                            ('stage_player_preload',player_preload()),
                            ('stage_player_entrance',player_entrance()),
                            ('stage_player_animation',player_set_animation()),
                            ('stage_ground_scan',ground_scan()),
                            ('stage_ground_kcl',ground_kcl()),
                            ('stage_line_scan',line_scan()),
                            ('stage_line_kcl',ground_kcl(line=True)),
                            ('stage_sphere_kcl',sphere_kcl()),
                            ('stage_camera_init',camera_init()),
                            ('stage_camera_render',camera_render()),
                            ('stage_view_render',view_render()),
                            ('stage_clipper_frustum',clipper_frustum())]:
        row['unit']=unit
        rows.append(row)
        bindings.extend(code)
    row,code=stage_areas();row['unit']='stage_areas'
    rows.append(row);bindings.extend(code)
    for row,code in overlay_initializers():
        rows.append(row); bindings.extend(code)
    for row,code in arm9_initializers():
        rows.append(row); bindings.extend(code)
    from stage_actor_views import slices
    for row,code in slices(projected,free_c,SYMBOLS,DATA_SYMBOLS):
        rows.append(row);bindings.extend(code)
    from stage_moving_views import slices as moving_slices
    for row,code in moving_slices(projected,free_c):
        rows.append(row);bindings.extend(code)
    from stage_water_views import slices as water_slices
    for row,code in water_slices(projected):
        rows.append(row);bindings.extend(code)
    from stage_hatena_views import slices as hatena_slices
    for row,code in hatena_slices(projected,SYMBOLS):
        rows.append(row);bindings.extend(code)
    from stage_isolated_views import slices as isolated_slices
    for row,code in isolated_slices(projected):
        rows.append(row);bindings.extend(code)
    from stage_player_updates import slices as player_updates
    for row,code in player_updates(projected):
        rows.append(row);bindings.extend(code)
    for name in ['func_ov002_020e5948','func_ov002_020e63a4',
                 'func_ov002_020e6330','func_ov002_020e6350','func_ov002_020e6780',
                 'func_ov002_020e6bb0']:
        row,code=free_c('external/sm64ds-pc/src/'+name+'.c')
        for binding in code: binding['overlay']=2
        rows.append(row); bindings.extend(code)
    # Player walk-physics core: upstream's port/unmatched C body (a near-miss
    # of the ROM whose documented divergences are register/schedule only).
    row,code=free_c('external/sm64ds-pc/port/unmatched/func_ov002_020d3b9c.c')
    for binding in code: binding['overlay']=2
    row['unit']='stage_unmatched_func_ov002_020d3b9c'
    rows.append(row); bindings.extend(code)
    for name,overlay in [('func_ov002_020d228c',2)]:
        row,code=free_c('external/sm64ds-pc/src/'+name+'.cpp')
        for binding in code: binding['overlay']=overlay
        rows.append(row); bindings.extend(code)
    for name in ['_ZN10ModelAnim213Func_020162C4Eji5Fix12IiEt',
                 '_ZN15TextureSequence7SetFileER8BTP_Filei5Fix12IiEj']:
        row,code=free_c('external/sm64ds-pc/src/'+name+'.c')
        rows.append(row); bindings.extend(code)
    for rel in ['src/Enable3dEngines.c','src/LoadOverlay.c','src/FS_LoadOverlay.c',
                'src/Vec3_RotateYAndTranslate.c',
                'src/Vec3_Asr.c','src/Vec3_AsrInPlace.c',
                'src/Matrix4x3_FromTranslation.c',
                'src/Matrix4x3_ApplyInPlaceToRotationY.c',
                'src/Matrix4x3_ApplyInPlaceToRotationX.c',
                'src/Matrix4x3_ApplyInPlaceToRotationZ.c',
                'src/Matrix4x3_ApplyInPlaceToTranslation.c',
                'src/Matrix4x3_FromRotationY.c','src/Matrix4x3_FromRotationX.c',
                'src/Matrix4x3_FromRotationZ.c','src/Matrix4x3_ApplyInPlaceToRotationXYZExt.c',
                'src/GetLevelPart.c','src/RandomIntInternal.c','src/AngleDiff.c',
                'src/SetBg0Offset.c','src/SetSubBg0Offset.c','src/SetSubBg1Offset.c','src/SetSubBg2Offset.c',
                'external/sm64ds-pc/src/func_0201eaac.c',
                'external/sm64ds-pc/src/StartWithFarCamera.c',
                'external/sm64ds-pc/src/GetViewObj.c',
                'external/sm64ds-pc/src/func_0200cf40.c',
                'external/sm64ds-pc/src/func_0200cb58.c',
                'external/sm64ds-pc/src/func_0200d954.c',
                'external/sm64ds-pc/src/func_0200cce4.c',
                'external/sm64ds-pc/src/func_020093f4.c',
                'external/sm64ds-pc/src/func_020093d4.c',
                'external/sm64ds-pc/src/func_0200897c.c',
                'external/sm64ds-pc/src/func_0200928c.c',
                'external/sm64ds-pc/src/func_0200d0ac.c',
                'src/unnamed/arm9/0201/func_02017e60.c',
                'src/unnamed/arm9/0201/func_02018a24.c',
                'src/unnamed/arm9/0201/func_0201973c.c',
                'src/unnamed/arm9/0203/func_0203b9b4.c',
                'src/unnamed/arm9/0204/func_020469e0.c',
                'src/unnamed/arm9/0207/func_020731dc.c',
                'src/unnamed/arm9/0201/func_02018c00.c',
                'src/unnamed/arm9/0201/func_02018cbc.c',
                'src/unnamed/arm9/0201/func_02018e00.c',
                'src/unnamed/arm9/0201/func_02018dc4.c',
                'src/unnamed/arm9/0201/func_02018b64.c',
                'src/unnamed/arm9/0203/func_0203d7b8.c',
                'src/unnamed/arm9/0203/func_0203d974.c',
                'src/unnamed/arm9/0205/func_0205dd9c.c',
                'src/unnamed/arm9/0205/func_0205e088.c',
                'src/unnamed/arm9/0205/func_0205e0b0.c',
                'src/unnamed/arm9/0206/func_0206d32c.c',
                'src/unnamed/arm9/0201/func_020190b8.c',
                'src/unnamed/arm9/0201/func_02019018.c',
                'src/unnamed/arm9/0203/func_0203da9c.c',
                'src/unnamed/arm9/0203/func_0203da3c.c',
                'src/unnamed/arm9/0203/func_0203dad4.c',
                'src/unnamed/arm9/0205/func_0205583c.c',
                'src/unnamed/arm9/0205/func_020556d0.c','src/func_02055780.c',
                'src/SublevelToLevel.c', 'src/LoadArchive.c',
                'src/UnloadArchive.c','src/LoadTextNarcs.c','src/ResetKuppaScript.c',
                'src/FS_InitFile.c','src/FS_ReadFile.c',
                'src/unnamed/arm9/0201/func_02018934.c',
                'src/unnamed/arm9/0201/func_02018908.c',
                'src/unnamed/arm9/0201/func_02018d98.c',
                'src/unnamed/arm9/0201/func_02018d48.c',
                'src/unnamed/arm9/0205/func_0205cd34.c',
                'src/unnamed/arm9/0205/func_0205cc80.c',
                'src/unnamed/arm9/0205/func_0205cb68.c',
                'src/unnamed/arm9/0205/func_0205d23c.c',
                'src/unnamed/arm9/0205/func_0205d304.c',
                'src/unnamed/arm9/0205/func_0205cbe4.c',
                'src/unnamed/arm9/0205/func_0205caa8.c',
                'src/unnamed/arm9/0205/func_0205c864.c',
                'src/unnamed/arm9/0205/func_0205d2b0.c',
                'src/unnamed/arm9/0205/func_0205d2dc.c']:
        row,code = free_c(rel)
        if rel.endswith('/func_02018b64.c'):
            # Native storage must exist before the unchanged image loader
            # clears BSS and reads the file. Its provider calls this ABI body.
            code = []
        rows.append(row); bindings.extend(code)
    rows.append(hardware_registers_row())
    dependencies={}
    for row in rows:
        for name,value in row.pop('dependencies').items():
            if name in dependencies and dependencies[name]!=value:
                raise ValueError('inconsistent stage dependency hash: '+name)
            dependencies[name]=value
    manifest = {'version':1, 'namespace':'stage_sources',
                'dependencies':dependencies,
                'entries':rows, 'bindings':bindings}
    (ROOT/'android/tools/stage_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

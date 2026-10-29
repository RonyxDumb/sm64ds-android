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
from lift_frontend import Lift, PARSER, DATA_SYMBOLS
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]
SYMBOLS = {}
for path in ('config/arm9/symbols.txt', 'config/arm9/itcm/symbols.txt'):
    for line in (ROOT/path).read_text().splitlines():
        match = re.match(r'(\S+) kind:function.*?addr:0x([0-9a-f]+)', line)
        if match:
            SYMBOLS[match[1]] = int(match[2], 16)
SYMBOLS['func_ov001_020ab2e4'] = 0x020ab2e4
SYMBOLS['_ZN10dScStage_c18LoadClsnAndObjectsER11LVL_OverlayjR7dBgW_Kc'] = 0x020fe190
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
struct Matrix2x2 { int m[4]; };
struct SharedFilePtr { u16 fileID; u8 numRefs; u8 pad; void *filePtr; };
struct LVL_Overlay { u8 bytes[0x1c]; };
struct MeshCollider { u8 bytes[0x50]; };
struct SysTracker { u8 bytes[0x81c]; };
struct Model { u8 bytes[0x50]; };
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
    return projected(rel,body,'void func_0204fa2c(int *,int);',['func_02011b7c'])


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
    shared = (ROOT/'include/decl_common.h').read_text()
    used = set(re.findall(r'\b(?:data_\w+|func_\w+|SUBLEVEL_LEVEL_TABLE)\b', raw))
    imports = [m.group() for m in re.finditer(r'^extern [^;\n]+;',shared,re.M)
               if '#include "decl_common.h"' in (ROOT/rel).read_text()
               and set(re.findall(r'\b(?:data_\w+|func_\w+|SUBLEVEL_LEVEL_TABLE)\b',m.group())) & used]
    raw = '\n'.join(imports)+'\n'+raw
    if rel.endswith('/func_0205cb68.c'):
        raw = replace_once(raw,'extern void func_0205d2dc(void);',
                           'extern int func_0205d2dc(void *,int,int,int);')
        raw = replace_once(raw,'extern void func_0205d2b0(void);',
                           'extern int func_0205d2b0(void *,const void *,unsigned int,unsigned int);')
    lift = Lift(raw)
    lift.result()
    exports = [name for name,_,_ in lift.exports]
    return projected(rel,raw,'',exports,auto_globals=False)


def graphics_3d():
    rel = 'src/engine/view/Graphics3D.cpp'
    raw = (ROOT/rel).read_text()
    body = function(raw,'void Initialise3dGraphics(int arg) {')
    body = body.replace('G3X::SetClearColor','_ZN3G3X13SetClearColorEtiiib')
    macros = '\n'.join(line for line in raw.splitlines() if line.startswith('#define REG_'))
    prototypes = '''
void func_0205583c(void); void func_020554bc(void); void func_020556d0(void);
void _ZN3G3X13SetClearColorEtiiib(u16,int,int,int,bool);
void _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,int,void *);
'''
    return projected(rel,body,prototypes,['Initialise3dGraphics'],extra=macros+'\n')


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
    return projected(rel,body,'',['_ZN8SaveData19IsCharacterUnlockedEj'])


def stage():
    rel = 'src/game/stages/d_s_stage_methods.cpp'
    raw = (ROOT/rel).read_text()
    body = function(raw, 'int dScStage_c::InitResources() {')
    body = replace_once(body, 'int dScStage_c::InitResources()', 'int _ZN10dScStage_c13InitResourcesEv(u32 actor)')
    body = replace_once(body, 'dScStage_c *scene = this;', 'Stage *scene = (Stage *)actor;')
    # The complete controller reset uses its shared, pointer-free 0x18 record.
    controls = re.search(r'struct CamInput \{.*?\n\};',raw,re.S).group()
    reset = function(raw,'extern "C" void ResetInput(void) {')
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
void UnloadArchive(int); void LoadTextNarcs(void); int LoadArchive(int);
void Initialise3dGraphics(int); void InitialiseVramGlobals(void);
void func_02039218(void); void ResetKuppaScript(void);
int GetSoundGroupID(int,int); void LoadDebugFont(void);
int IsLevelInsideCastle(int); int IsLevelTinyHugeIslandOutside(int);
int func_020308a8(void); int func_0203da9c(void); int func_0203da3c(void);
u32 func_0203dad4(void); void func_0203b9b4(int *,int);
void *func_02073470(int,int,int,void *,void *); void func_ov001_020ab2e4(void);
void *_ZN9dFdWipe_cC1Ev(void *); void _ZN9dFdWipe_cD1Ev(void *);
void _ZN10dScStage_c14LoadGraphics2DEbi(int,int);
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
'''
    return projected(rel,reset+'\n'+body,prototypes,
                     ['ResetInput','_ZN10dScStage_c13InitResourcesEv'],extra=controls+'\n')


if __name__ == '__main__':
    rows,bindings = [],[]
    for row,code in [stage(),sound(),narc(),scene_graphics(),graphics_3d(),collision_reset(),cap_reset(),save_predicate()]:
        rows.append(row); bindings.extend(code)
    for rel in ['src/Enable3dEngines.c',
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
        rows.append(row); bindings.extend(code)
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

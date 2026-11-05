#!/usr/bin/env python3
"""Audited native pointer views of the original minigame menu and OAM.

Regeneration needs the same pinned tree-sitter packages as lift_frontend.py.
Builds consume the hash-guarded manifest with no parser dependency.
"""
import hashlib
import json
import re
from pathlib import Path
from lift_frontend import Lift, PARSER, DATA_SYMBOLS
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]
DATA = {
    'sMiniGmGroupBases': 0x020c2250, 'sMiniGmBgFiles': 0x020c2260,
    'sMiniGmRecordPos': 0x020c2280, 'sMiniGmBadgePos': 0x020c22c8,
    'sMiniGmIconPalettes': 0x020c2310, 'sMiniGmIconChars': 0x020c23a0,
    'sMiniGmIconScreenOffsets': 0x020c2464, 'sMiniGmRows': 0x020c24d8,
    'sMiniGmSaveTable': 0x020c24dc, 'sMiniGmGroupTabs': 0x020c2c28,
    'sMiniGmExitButton': 0x020c2e88, 'sMiniGmPageArrows': 0x020c2ea4,
    'sMiniGmDigits': 0x020c2efc, 'sMiniGmRecordLabels': 0x020c2f4c,
    'sMiniGmNewBadge': 0x020c2f60, 'sMiniGmSavingCaption': 0x020c2f88,
    'sMiniGmCaption': 0x020c2fcc,
}
DATA_SYMBOLS.update(DATA)


class MenuLift(Lift):
    def address(self, name):
        return DATA[name] if name in DATA else super().address(name)


def unit(path, text, dependencies=()):
    return {'path': path, 'sha256': digest(path), 'header': False,
            'dependencies': {p: digest(p) for p in dependencies},
            'source': MenuLift(text).result()}


def digest(path):
    return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()


TYPES = '''
typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;
typedef unsigned int u32; typedef int s32;
typedef long long s64; typedef unsigned long long u64;
struct Pair { int x, y; }; struct Off9 { int v[9]; };
struct TouchRec { u8 held, edge, x, y; };
struct OamAttr { u16 attr0, attr1, attr2, attr3; };
struct MgRecordDefaults { s32 v[5]; };
struct MgEntry { u16 id, pad; s32 sceneParam, scoringMode, scoreCap, ownRecords;
    MgRecordDefaults recordDefaults; s32 musicTrack, musicMode, level; };
struct dScMiniGm_c {
    u8 base[0x50]; s32 mSubBgScrollX; u8 mPageFlipped; u8 pad055[3];
    s32 mGroupBase, mStartLevel, unk_060, mGfxSlot, mGfxSlots[9];
    s32 mArrowBobPhase, mPrevPageTimer, mNextPageTimer, mExitTimer;
    s32 mIconBlinkPhase, mScrollDelay, unk_0a4, unk_0a8;
    u8 mExiting, pad0ad[3];
};
static_assert(sizeof(dScMiniGm_c)==0xb0);
static_assert(offsetof(dScMiniGm_c,mGfxSlots)==0x68);
static_assert(offsetof(dScMiniGm_c,mExiting)==0xac);
static_assert(sizeof(MgEntry)==0x34);
'''

PROTOTYPES = '''
void MiniGm_LoadGroupIcons(void *); void MiniGm_LoadIconPalette(char *,int);
void MiniGm_DrawRecords(void *); void MiniGm_DrawNumber(void *,int,int,int);
void MiniGm_DrawNewBadges(void *); void MiniGm_UpdateGroupTabs(char *);
void MiniGm_UpdatePageArrows(char *); void MiniGm_UpdateGameSelect(dScMiniGm_c *);
void MiniGm_UpdateExitButton(char *); void MiniGm_UpdatePageScroll(char *);
int MiniGm_HasSecondPage(void *); int MiniGm_IsUnlocked(void *,int);
int MiniGm_IsNew(void *,int); void MiniGm_ClearNew(void *,int);
int MiniGm_GetSaveTable(void *,int); int MiniGm_GetRecord(void *,int,int);
void MiniGm_SetRecord(void *,int,int,int); void MiniGm_SaveIfDirty(char *);
void mg_LoadTextNarcs(); int mg_LoadArchive(int); void mg_ResetFiles();
void mg_LoadGroup(int,int); void mg_Music(int); void mg_StopMusic(unsigned);
void mg_UnsetVoice(); int mg_Effect(int); int mg_Language();
char *mg_LoadFile(int); void mg_Free(void *); void mg_InitVram();
int mg_FaderReady(); void mg_SetFaders(void *); void mg_StopColorFader();
void mg_WipeType(void *,int); void mg_StartScene(int,int,int); void mg_ExitMenu();
int mg_CanStartGame(int);
void mg_Save(void *); void mg_ClearNew(int); int mg_GetRecord(int,int);
void mg_SetRecord(int,int,int); int mg_IsNew(int); int mg_IsUnlocked(int);
void mg_ResetGeometry(); void mg_Cache(u32,u32);
int RandomIntInternal(int *);
void mg_DecompressLZ16(void *,void *); void MultiStore16(u16,void *,int);
void MultiCopy_Int(int *,int *,int);
void mg_Oam10(bool,OamAttr *,int,int,int,int,int,int,int,int);
OamAttr *mg_Oam8(bool,OamAttr *,int,int,int,int,int,int);
void mg_OamSub(OamAttr *,int,int,int,int);
void GX_SetBankForBG(u16); void GX_SetBankForOBJ(u16);
void GX_SetBankForSubBG(u16); void GX_SetBankForSubOBJ(u16);
void GX_SetBankForTex(u16); void GX_SetBankForTexPltt(u16);
void mg_DisableBanks(); void GX_SetGraphicsMode(int,int,int);
void GXS_SetGraphicsMode(int); void GX_DispOn(); void mg_Fog(int,int,int,int);
void *G2_GetBG2CharPtr(); void *G2_GetBG2ScrPtr();
void *G2S_GetBG0CharPtr(); void *G2S_GetBG0ScrPtr();
void *G2S_GetBG2CharPtr(); void *G2S_GetBG2ScrPtr(); void *G2S_GetBG3ScrPtr();
void GX_LoadBGPltt(const void *,u32,u32); void GX_LoadOBJPltt(const void *,u32,u32);
void GXS_LoadBGPltt(const void *,u32,u32); void GXS_LoadOBJPltt(const void *,u32,u32);
void mg_SetBg2Offset(int,int); void mg_SetSubBg2Offset(int,int);
void mg_SetSubBg3Offset(int,int); void mg_SetSubBg0Offset(int,int);
'''


def menu():
    rel = 'src/minigames/d_s_mini_gm.cpp'
    raw = (ROOT/rel).read_text()
    nodes = []
    def visit(node):
        if node.type == 'function_definition':
            nodes.append(node)
            return
        for child in node.named_children:
            visit(child)
    visit(PARSER.parse(raw.encode()).root_node)
    bodies = []
    fields = re.findall(r'\b(?:m\w+|unk_\w+)\b', TYPES)
    for node in nodes:
        text = raw.encode()[node.start_byte:node.end_byte].decode()
        if 'dScMiniGm_c_classInit(' in text:
            continue  # native owner allocates the exact POD, no host C++ vtable
        if 'dScMiniGm_c::' in text.split('{', 1)[0]:
            name = re.search(r'dScMiniGm_c::(\w+)\(', text)[1]
            out = 'MiniGm_UpdateGameSelect' if name == 'UpdateGameSelect' else 'mg_Menu'+name
            text = re.sub(r'dScMiniGm_c::\w+\(\)', out+'(dScMiniGm_c *self)', text, count=1)
            text = re.sub(r'\bthis\b', 'self', text)
            head, body = text.split('{', 1)
            for field in set(fields):
                body = re.sub(r'(?<![\w>.])\b'+field+r'\b', 'self->'+field, body)
            body = body.replace('UpdateGameSelect();', 'MiniGm_UpdateGameSelect(self);')
            text = head+'{'+body
        bodies.append(text)
    body = '\n'.join(bodies)
    # dFader is a native-owned polymorphic service. Never read its host vtable
    # through the cartridge's 32-bit layout; preserve every input gate.
    for line in [
        '    void* g = (void*)data_0209f5bc;\n',
        '    int (**vt)(void*) = *(int(***)(void*))g;\n',
        '    int (**vt)(void*);\n', '    void *obj = (void*)data_0209f5bc;\n',
        '    vt = *(int(***)(void*))obj;\n', '    int* g = (int*)data_0209f5bc;\n']:
        if line not in body: raise ValueError('fader gate changed: '+line)
        body = body.replace(line, '')
    for call in ('vt[5](g)', 'vt[5](obj)', 'data_0209f5bc->IsActive()',
                 '(*(int(***)(int*))g)[0x14/4](g)'):
        if call not in body: raise ValueError('fader call changed: '+call)
        body = body.replace(call, 'mg_FaderReady()')
    body = replace_once(body, 'data_0209f61c.func_0202ec9c(0)', 'mg_WipeType(&data_0209f61c,0)')
    # A visible, cartridge-unlocked icon does not establish a native gameplay
    # binding. Reject unsupported routes before palette/save/fader mutations;
    # never freeze the menu or claim that an unlinked game was launched.
    body = replace_once(body, '    if (MiniGm_IsUnlocked(self, sel) == 0) return;',
        '    if (MiniGm_IsUnlocked(self, sel) == 0) return;\n'
        '    if (!mg_CanStartGame(sMiniGmRows[sel].id)) { mg_Effect(0xe); return; }')
    # The recovered byte-clear helper has a negative shift for index<28.
    # The checked native service implements the same bit updates without UB.
    aliases = {
        'LoadTextNarcs':'mg_LoadTextNarcs', 'LoadArchive':'mg_LoadArchive',
        'func_02019028':'mg_ResetFiles', 'InitialiseVramGlobals':'mg_InitVram',
        'func_020233f4':'mg_ResetGeometry', 'LoadFile':'mg_LoadFile',
        'Deallocate':'mg_Free', 'GetOwnerLanguage':'mg_Language',
        'DecompressLZ16':'mg_DecompressLZ16',
        'func_02012790':'mg_Effect', 'ExitMinigameMenu':'mg_ExitMenu',
        'func_02013568':'mg_SetRecord', 'func_02013580':'mg_GetRecord',
        'func_02013598':'mg_ClearNew', 'func_020135d0':'mg_IsNew',
        'func_02013638':'mg_IsUnlocked', 'SaveData::SaveMinigames':'mg_Save',
        'Sound::UnsetPlayerVoiceGroup':'mg_UnsetVoice',
        'Sound::StopLoadedMusic_Layer1':'mg_StopMusic',
        '_ZN5Sound22StopLoadedMusic_Layer1Ej':'mg_StopMusic',
        '_ZN5Sound19LoadGroupAndSetBankEii':'mg_LoadGroup',
        '_ZN5Sound22LoadAndSetMusic_Layer1Ei':'mg_Music',
        '_ZN8dScene_c9SetFadersEP15dFdBrightness_c':'mg_SetFaders',
        '_ZN8dScene_c20SetAndStopColorFaderEv':'mg_StopColorFader',
        'dScene_c::StartSceneFade':'mg_StartScene',
        '_ZN2GX15DisableAllBanksEv':'mg_DisableBanks',
        '_ZN2GX13SetBankForTexEt':'GX_SetBankForTex',
        '_ZN2GX17SetBankForTexPlttEt':'GX_SetBankForTexPltt',
        '_ZN2GX15SetGraphicsModeEiii':'GX_SetGraphicsMode',
        '_ZN3GXS15SetGraphicsModeEi':'GXS_SetGraphicsMode',
        '_ZN2GX6DispOnEv':'GX_DispOn', '_ZN3G3X6SetFogEbiii':'mg_Fog',
        '_ZN2G213GetBG2CharPtrEv':'G2_GetBG2CharPtr',
        '_ZN2G212GetBG2ScrPtrEv':'G2_GetBG2ScrPtr',
        'CP15::FlushAndInvalidateDataCache':'mg_Cache',
        'SetBg2Offset':'mg_SetBg2Offset', 'SetSubBg2Offset':'mg_SetSubBg2Offset',
        'SetSubBg3Offset':'mg_SetSubBg3Offset', 'SetSubBg0Offset':'mg_SetSubBg0Offset',
    }
    for name in ('GX','GXS','G2S'):
        body = body.replace(name+'::', name+'_')
    for old,new in aliases.items():
        body = re.sub(r'(?<!\w)'+re.escape(old)+r'\b',new,body)
    # Resolve the two Render overloads by their original argument count.
    source = body.encode()
    edits = []
    def calls(node):
        if node.type=='call_expression':
            fn=node.child_by_field_name('function')
            name=source[fn.start_byte:fn.end_byte].decode()
            if name=='OAM::Render':
                count=len(node.child_by_field_name('arguments').named_children)
                if count not in (8,10): raise ValueError('new OAM Render overload')
                edits.append((fn.start_byte,fn.end_byte,'mg_Oam'+str(count)))
            elif name=='OAM::RenderSub':
                edits.append((fn.start_byte,fn.end_byte,'mg_OamSub'))
        for child in node.named_children: calls(child)
    calls(PARSER.parse(source).root_node)
    for start,end,text in sorted(edits,reverse=True):
        source=source[:start]+text.encode()+source[end:]
    # Keep the original source's declarations, replacing only the two native
    # polymorphic service types with opaque, correctly sized address records.
    declarations = raw[raw.index('extern u8 gMgSaveDirty;'):raw.index('/* ---- this TU')]
    declarations += '\nextern int gMgGroupBase, gMgLastSelected[];\n'
    declarations += '\nextern const int sMiniGmGroupBases[];\nextern int sMiniGmGroupTabs[],sMiniGmPageArrows[],sMiniGmNewBadge[];\n'
    declarations += 'extern int sMiniGmExitButton,sMiniGmSavingCaption,sMiniGmCaption;\n'
    declarations += 'extern int data_0209b2fc,data_0209d4b8; extern u8 data_0209f1d8;\n'
    declarations = declarations.replace('extern TouchOwner *data_0209f5bc;', '')
    declarations = declarations.replace('extern dWipe_c data_0209f61c;', 'extern u8 data_0209f61c;')
    declarations = declarations.replace('extern MinigameSaveData data_0209cae4;', 'extern u8 data_0209cae4;')
    return unit(rel,TYPES+declarations+PROTOTYPES+source.decode(),
                ('include/dScMiniGm_c.h','include/MgMenuRow.h'))


def oam():
    rel='src/engine/oam/OAM.cpp'
    raw=(ROOT/rel).read_text()
    names={
        'int OAM::LoadAffineParams(OamAttr *oam, int *count, Matrix2x2 *mtx)':'mg_OamAffine',
        'u8 OAM::GetObjHeight(int shape, int sizeBits)':'mg_OamHeight',
        'u8 OAM::GetObjWidth(int shape, int sizeBits)':'mg_OamWidth',
        'void OAM::Render(bool sub, struct OamAttr *attr_, int xOff, int yOff, int palette, int priority,\n                 int scaleX, int scaleY, int rotation, int mode)':'mg_Oam10',
        'OamAttr *OAM::Render(bool draw, OamAttr *obj_, int px, int py, int pal, int prio, int scale0,\n                     int rot0)':'mg_Oam8',
        'void OAM::RenderSub(OamAttr *data, s32 x, s32 y, s32 palette, s32 priority)':'mg_OamSub',
        'void OAM::Reset()':'mg_OamReset', 'void OAM::Flush()':'mg_OamFlush',
        'void OAM::Load()':'mg_OamLoad',
    }
    body='\n'.join(function(raw,sig) for sig in names)
    for sig,name in names.items():
        body=body.replace(sig, re.sub(r'OAM::\w+',name,sig))
    aliases={'_ZN3OAM11GetObjWidthEii':'mg_OamWidth',
             '_ZN3OAM12GetObjHeightEii':'mg_OamHeight',
             '_ZN3OAM16LoadAffineParamsEP7OamAttrPiP9Matrix2x2':'mg_OamAffine',
             'OAM::GetObjWidth':'mg_OamWidth','OAM::GetObjHeight':'mg_OamHeight',
             'OAM::LoadAffineParams':'mg_OamAffine','OAM::Render':'mg_Oam10',
             'CP15::FlushAndInvalidateDataCache':'mg_Cache'}
    for old,new in aliases.items(): body=body.replace(old,new)
    body=body.replace('MultiCopy_Int(', 'mg_CopyWords(').replace('MultiCopy32Bytes(', 'mg_CopyWords(')
    for table in ('data_020755a0','data_020755ac'):
        body=replace_once(body,'*(unsigned char *)&((int *)((char *)'+table+' + sizeBits))[shape]',
                          table+'[sizeBits + shape * 4]')
    records=raw[raw.index('struct Matrix2x2 {'):raw.index('extern "C" {')]
    # Lift requires one bitfield declarator per declaration. Width/order stay
    # identical to the original 8-byte little-endian hardware records.
    records=re.sub(r'u32 ([^;{}]+);',lambda m: 'u32 '+m[1].replace(',', '; u32')+';'
                   if ':' in m[1] else m[0],records)
    records+='\nstatic_assert(sizeof(OamAttrRaw)==8); static_assert(sizeof(OamAttrTpl)==8);\n'
    pre='''
struct OamAttr { u16 attr0,attr1,attr2,attr3; };
int mg_OamAffine(OamAttr *,int *,Matrix2x2 *); u8 mg_OamHeight(int,int); u8 mg_OamWidth(int,int);
void mg_Oam10(bool,OamAttr *,int,int,int,int,int,int,int,int);
void mg_Cache(u32,u32); void mg_CopyWords(int *,int *,int);
int _ZN4cstd4fdivEii(int,int); void func_020566dc(const void *,u32,u32);
void func_02056674(const void *,u32,u32);
'''
    return unit(rel, TYPES[:TYPES.index('struct Pair')]+records+pre+body, ('include/OamAttr.h',))


def main():
    from lift_minigame_state import generate
    from lift_minigame_records import generate as records
    state,bindings,_=generate(digest)
    record_sources,record_bindings=records(digest)
    for binding in record_bindings: binding['overlay']=4
    data={'version':1,'namespace':'minigame_sources','entries':[menu(),oam(),state,record_sources],
          'bindings':bindings+record_bindings,
          'dependencies':{p:digest(p) for p in ('config/arm9/symbols.txt','config/arm9/itcm/symbols.txt')}}
    (ROOT/'android/tools/minigame_manifest.json').write_text(json.dumps(data,indent=2)+'\n')


if __name__=='__main__': main()

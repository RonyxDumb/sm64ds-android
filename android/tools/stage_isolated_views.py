"""Reviewed views of recovered isolated member units (external/sm64ds-pc/src).

Each unit is one original function whose calls are already spelled as their
mangled ABI symbols. Only the receiver changes: `this` becomes the cartridge
object address and named fields become their header-recorded offsets. The
body, branches and constants stay the recovered function's own.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCALARS = {'u8': 'u8', 's8': 's8', 'u16': 'u16', 's16': 's16', 'u32': 'u32',
           's32': 's32', 'int': 's32', 'Fix12i': 's32', 'short': 's16',
           'char': 's8', 'bool': 'u8', 'unsigned': 'u32', 's64': 's64', 'u64': 'u64'}
FIELD = re.compile(r'^\s+(?:unsigned\s+)?([A-Za-z_]\w*)\s*(\*?)\s*(\w+)\s*(\[\w+\])?\s*;\s*/\*\s*0x([0-9a-f]+)')


def header_fields(rel, minimum=0):
    fields = {}
    for line in (ROOT/rel).read_text().splitlines():
        match = FIELD.match(line)
        if not match:
            continue
        typ, pointer, name, array, offset = match[1], match[2], match[3], match[4], int(match[5], 16)
        if offset < minimum:
            continue
        if array:
            # Arrays decay to a typed element pointer at the member offset.
            if pointer or typ in SCALARS:
                fields[name] = ('[]'+('u32' if pointer else SCALARS[typ]), offset)
            continue
        if pointer:
            fields[name] = ('u32', offset)
        elif typ in SCALARS:
            fields[name] = (SCALARS[typ], offset)
    return fields


def player_fields():
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS)
    fields.update(header_fields('include/Player.h', 0xd0))
    return fields


def shared_declarations(raw):
    """Imports from the unit's decl_*.h headers, limited to names it uses."""
    text = re.sub(r'^\s*#[^\n]*', '', raw, flags=re.M)
    used = set(re.findall(r'\b\w+\b', text))
    # Names the unit declares itself keep the unit's own spelling.
    own = set(re.findall(r'^\s*extern\b[^;{]*?(\w+)\s*(?:\(|\[|;)', text, re.M))
    found = []
    for header in re.findall(r'#include "(decl_\w+\.h)"', raw):
        for rel in ('external/sm64ds-pc/include/'+header, 'include/'+header):
            if (ROOT/rel).exists():
                for line in re.findall(r'^extern [^;\n{}]+;', (ROOT/rel).read_text(), re.M):
                    name = re.search(r'(\w+)\s*(?:\(|\[|;)', line)
                    if name and name[1] in used and name[1] not in own:
                        found.append(line)
                break
    return '\n'.join(dict.fromkeys(found))


def isolated(projected, symbol, overlay, cls, fields, extra='', replacements=None, rel=None, name=None,
             types=None, prelude=''):
    rel = rel or 'external/sm64ds-pc/src/'+symbol+'.cpp'
    raw = (ROOT/rel).read_text()
    if '::' in cls:
        # A nested class (Particle::SysTracker): drop the enclosing namespace
        # block so the member body is matched as Inner::method.
        outer, cls = cls.split('::')
        opened = re.search(r'^namespace '+outer+r'\s*\{\s*$', raw, re.M)
        if opened:
            raw = raw[:opened.start()]+raw[opened.end():]
            close = raw.rstrip().rfind('}')
            raw = raw[:close]+raw[close+1:]
    if name:
        from native_sources import function
        raw = function(raw, name)
        # In-body file-scope externs become ordinary declarations.
        hoisted = re.findall(r'^\s*(extern [^;\n(]+;)\s*$', raw, re.M)
        raw = re.sub(r'^\s*extern [^;\n(]+;\s*$\n', '', raw, flags=re.M)
        for old, new in (types or {}).items():
            hoisted = [re.sub(r'\b'+old+r'\b', new, line) for line in hoisted]
        raw = prelude+'extern "C" {\n'+'\n'.join(hoisted)+'\n}\n'+raw
    shared = shared_declarations(raw)
    raw = re.sub(r'^\s*#(?:include|pragma)[^\n]*\n', '', raw, flags=re.M)
    for old, new in (replacements or {}).items():
        raw = raw.replace(old, new)
    declarations = [shared] if shared else []
    for block in re.findall(r'extern "C" \{\n(.*?)\n\}\n', raw, re.S):
        declarations.append(block)
    raw = re.sub(r'extern "C" \{\n.*?\n\}\n', '', raw, flags=re.S)
    signature = re.search(r'^(\w[\w\s\*]*?[\s\*])\s*'+cls+r'::(\w+)\(([^)]*)\)\s*(?:const\s*)?\{', raw, re.M)
    if not signature:
        raise ValueError('no member body in '+rel)
    result = signature[1].strip()
    if '*' in result:
        result = 'void *'  # object pointers are opaque four-byte handles here
    params = signature[3].strip()
    params = '' if params in ('', 'void') else ', '+params
    start = signature.start()
    body = raw[:start]+'extern "C" '+result+' '+symbol+'(void *object'+params+') {'+raw[signature.end():]
    for old, new in (replacements or {}).items():
        body = body.replace(old, new)
    body = body.replace('((char*)this)', '((char *)object)').replace('(char*)this', '(char *)object')
    body = re.sub(r'\bthis\b', 'object', body)
    for name, (typ, offset) in fields.items():
        if typ.startswith('[]'):
            body = re.sub(r'(?<![.>\w])'+name+r'\b',
                          '(('+typ[2:]+' *)((char *)object+0x%x))' % offset, body)
            continue
        body = re.sub(r'(?<![.>\w])'+name+r'\b',
                      '(*('+typ+' *)((char *)object+0x%x))' % offset, body)
    # Older isolated units spell unnamed members unk_<offset>; take the type
    # the reviewed header records at that same offset.
    by_offset = {offset: typ for typ, offset in fields.values() if not typ.startswith('[]')}
    def unknown(match):
        offset = int(match[1], 16)
        if offset not in by_offset:
            raise ValueError('untyped member unk_%x in %s' % (offset, rel))
        return '(*('+by_offset[offset]+' *)((char *)object+0x%x))' % offset
    body = re.sub(r'(?<![.>\w])unk_([0-9a-f]+)\b', unknown, body)
    declarations = '\n'.join(declarations)
    # Local record types the imported declarations already name must precede
    # them: hoist top-level struct definitions of the unit into the prelude.
    local_types = ''
    records = list(re.finditer(r'^struct (\w+) \{\n.*?^\};\n', body, re.M | re.S))
    records += list(re.finditer(r'^typedef struct\s*\w*\s*\{.*?^\}\s*(\w+);\n', body, re.M | re.S))
    for match in records:
        if re.search(r'\b'+match[1]+r'\b', declarations):
            local_types += match[0]
            body = body.replace(match[0], '', 1)
    # Opaque class handles: the bodies only pass these pointers through.
    opaque = sorted(set(re.findall(r'\b(\w+_c|Player)\b', declarations+body)) - {symbol})
    extra = ''.join('struct %s;\n' % name for name in opaque
                    if not re.search(r'struct\s+'+name+r'\s*\{', extra+body)) + extra
    for vector in ('struct Vector3 { s32 x,y,z; };\n', 'struct Vector3_16 { s16 x,y,z; };\n'):
        name = vector.split()[1]
        if re.search(r'\b'+name+r'\b', declarations+body) and not re.search(
                r'struct\s+'+name+r'\s*\{|\}\s*'+name+r'\s*;', extra+body+declarations):
            extra += vector
    row, bindings = projected(rel, body, declarations, [symbol], extra=extra+local_types, auto_globals=False)
    row['unit'] = 'stage_isolated_'+symbol
    for binding in bindings:
        binding['overlay'] = overlay
    return row, bindings


PLAYER_STATES = [
    '_ZN6Player12St_Wait_InitEv',
]


VECTOR_TYPES = 'struct Vector3 { s32 x,y,z; }; struct Vector3_16 { s16 x,y,z; };\n'


def free_unit(projected, symbol, overlay, replacements=None):
    """A recovered isolated free function: raw offsets, mangled callees."""
    for ext in ('.c', '.cpp'):
        rel = 'external/sm64ds-pc/src/'+symbol+ext
        if (ROOT/rel).exists():
            break
    raw = (ROOT/rel).read_text()
    shared = shared_declarations(raw)
    raw = re.sub(r'^\s*#(?:include|pragma)[^\n]*\n', '', raw, flags=re.M)
    for old, new in (replacements or {}).items():
        raw = raw.replace(old, new)
    if shared:
        raw = shared+'\n'+raw
    extra = ''
    if not re.search(r'struct Vector3\s*\{|\}\s*Vector3\s*;', raw):
        extra += 'struct Vector3 { s32 x,y,z; };\n'
    if not re.search(r'struct Vector3_16\s*\{|\}\s*Vector3_16\s*;', raw):
        extra += 'struct Vector3_16 { s16 x,y,z; };\n'
    row, bindings = projected(rel, raw, '', [symbol], extra=extra, auto_globals=False)
    row['unit'] = 'stage_isolated_'+symbol
    for binding in bindings:
        binding['overlay'] = overlay
    return row, bindings


FREE_UNITS = [
    ('Vec3_LslInPlace', 0),
    ('Vec3_Add', 0),
    ('Vec3_MulScalar', 0),
    ('Math_Function_0203b0fc', 0),
    ('func_ov002_020f7538', 2),
    ('func_ov002_020f7410', 2, {
        # `register` is not C++17; the 16-bit angle store keeps its ROM value.
        'register int *p': 'int *p',
        '*(short *)(self + 0x8e) = 0xffffb3c0;': '*(short *)(self + 0x8e) = (short)-0x4c40;'}),
    ('func_0201ef38', 0),
    ('func_ov002_020f6618', 2, {
        # The unit's local SharedFilePtr {int a, file;} is the shared 8-byte
        # record (file pointer at +4); references are pointers in the ABI.
        'struct SharedFilePtr { int a, file; };\n': '',
        '(SharedFilePtr& f)': '(SharedFilePtr* f)',
        'void* _Znwj(unsigned int sz);': 'void* _Znwj(unsigned int sz);\nvoid _ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(void*, void*);',
        '_ZN5Model8LoadFileER13SharedFilePtr(**(SharedFilePtr**)(self + 0x70))':
            '_ZN5Model8LoadFileER13SharedFilePtr(*(SharedFilePtr**)(self + 0x70))',
        '_ZN9Animation8LoadFileER13SharedFilePtr(*(*(SharedFilePtr***)(self + 0x74))[i])':
            '_ZN9Animation8LoadFileER13SharedFilePtr((*(SharedFilePtr***)(self + 0x74))[i])',
        '(void*)(*(*(SharedFilePtr***)(self + 0x74))[0]).file':
            '(void*)(*(SharedFilePtr***)(self + 0x74))[0]->filePtr',
        '(void*)(*(SharedFilePtr**)(self + 0x70))->file': '(void*)(*(SharedFilePtr**)(self + 0x70))->filePtr',
        '_ZN15TextureSequence8LoadFileER13SharedFilePtr(*(*(SharedFilePtr***)(self + 0x78))[i])':
            '_ZN15TextureSequence8LoadFileER13SharedFilePtr((*(SharedFilePtr***)(self + 0x78))[i])',
        'TextureSequence::Prepare(*(BMD_File*)bmd, *(BTP_File*)btp);':
            '_ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(bmd, btp);',
        '(void*)(*(*(SharedFilePtr***)(self + 0x78))[0]).file':
            '(void*)(*(SharedFilePtr***)(self + 0x78))[0]->filePtr'}),
    ('func_ov002_020f7020', 2),
    ('func_ov002_020f6960', 2, {
        # A reference parameter is a pointer in the ARM ABI.
        'LoadFileER13SharedFilePtr(SharedFilePtr&)': 'LoadFileER13SharedFilePtr(SharedFilePtr*)',
        '_ZN5Model8LoadFileER13SharedFilePtr(*(*(SharedFilePtr**)(self + 0x5c)))':
            '_ZN5Model8LoadFileER13SharedFilePtr(*(SharedFilePtr**)(self + 0x5c))'}),
    ('func_ov002_020f6a50', 2, {
        # Callbacks are the original code addresses (Vector3 C1/D1), not
        # zero-argument host veneers.
        '(void*)func_0203d384, (void*)_ZN7Vector3D1Ev': '(void*)0x0203d384u, (void*)0x020072c0u',
        'extern void _ZN7Vector3D1Ev(void);\n': '', 'extern void func_0203d384(void);\n': ''}),
    ('Vec3_Sub', 0),
    ('func_020089f8', 0),
    ('func_02009414', 0),
    ('func_020094a8', 0),
    ('func_020082a0', 0),
    ('func_ov002_020bd600', 2),
    ('func_0200e5fc', 0, {
        # Same volatile byte read-modify-write; the packed reference has no +=.
        '*pb += 1;': '*pb = *pb + 1;'}),
    ('func_0200e6d8', 0),
    ('func_0200e5ac', 0),
    ('func_02013524', 0),
    ('func_02013078', 0),
    ('func_02049764', 0),
    ('func_02011dcc', 0),
    ('func_ov002_020f2630', 2),
    ('func_0200e4e4', 0),
    ('func_02043f98', 0),
    ('func_0200e55c', 0),
    ('func_0200e494', 0),
    ('RunKuppaScript', 0),
    ('StartIntroCutscene', 0),
    ('IsStarCollected', 0), ('IsStarCollectedInLevel', 0), ('IsStarCollectedInCurLevel', 0),
    ('SetStarMarker', 0), ('ContinueKuppaScriptIfNecessary', 0),
    ('func_0202a958', 0), ('func_0202a96c', 0), ('func_0202a980', 0),
    ('GetMinimapID', 0), ('GetMinimapScale', 0),
    ('func_ov002_020f65ec', 2),
    ('func_ov002_020f64ac', 2),
    ('func_ov002_020f20f4', 2),
    ('func_ov002_020f23f0', 2),
    ('Vec3_DivScalarInPlace', 0),
    ('Vec3_MulScalarInPlace', 0),
    ('func_0203dafc', 0),
    ('IsAreaShowing', 0),
    ('func_0204302c', 0),
    ('func_0203b27c', 0),
    ('func_0203b3c0', 0),
    ('func_0203db3c', 0),
    ('func_ov001_020aa960', 1),
    ('_ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh', 0, {
        # Clipper.h layout: vptr, four plane normals at 0x04, near/far 0x50/0x54.
        '#pragma opt_propagation off\n': '',
        'extern "C" int _ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh(':
            'struct ClipperPlane { s32 x; s32 y; s32 z; };\n'
            'struct Clipper { u8 pad_000[4]; ClipperPlane mPlaneNormals[4]; s32 mAspectRatio;\n'
            '    s32 mNearZ; s32 mFarZ; u16 mFovAngle; };\n'
            'extern "C" int _ZN7Clipper13Func_020150E8ER7Vector35Fix12IiEPh('}),
    ('_ZN7Minimap21FixTHIPaintingRoomPosER7Vector3', 2, {
        # A static member: its reference parameter is the ARM pointer word.
        'void Minimap::FixTHIPaintingRoomPos(Vector3 & v_)\n{\n    struct Vector3* v = &v_;':
            'extern "C" void _ZN7Minimap21FixTHIPaintingRoomPosER7Vector3(struct Vector3 *v)\n{'}),
    ('_ZN7Minimap15GetPosOnMinimapER7Vector3S1_5Fix12IiEsS1_', 2),
    ('_ZN7Minimap20GetPosFromMinimapPosER7Vector3S1_5Fix12IiEsS1_', 2, {
        # References are ARM pointer words.
        'struct Vec3& a, struct Vec3& b, int c, short d, struct Vec3& e':
            'struct Vec3* a, struct Vec3* b, int c, short d, struct Vec3* e',
        'Vec3_Sub(&local, &a, &data_ov002_0211116c);': 'Vec3_Sub(&local, a, &data_ov002_0211116c);',
        'Vec3_RotateYAndTranslate(&e, &b, d, &local);': 'Vec3_RotateYAndTranslate(e, b, d, &local);'}),
    ('_ZN3OAM6RenderEbP7OamAttriiii5Fix12IiES3_ii', 0, {
        # The stage prelude already defines Matrix2x2 as an int[4] record.
        '#pragma opt_common_subs off\n': '', '#pragma opt_propagation off\n': '',
        '#pragma opt_loop_invariants off\n': '',
        'struct Matrix2x2 { int a, b, c, d; };': 'struct OamAffine2 { int a, b, c, d; };',
        'struct Matrix2x2 *m);': 'struct OamAffine2 *m);',
        '    struct Matrix2x2 m;': '    struct OamAffine2 m;'}),
    # Static OAM size-table lookups: free functions in the ARM ABI.
    ('_ZN3OAM11GetObjWidthEii', 0, {
        'u8 OAM::GetObjWidth(int shape, int sizeBits)': 'extern "C" u8 _ZN3OAM11GetObjWidthEii(int shape, int sizeBits)',
        # The same byte, read without binding an int reference at a byte offset.
        'return *(unsigned char *)&((int *)((char *)data_020755a0 + sizeBits))[shape];':
            'return *((unsigned char *)data_020755a0 + sizeBits + shape * 4);'}),
    ('_ZN3OAM12GetObjHeightEii', 0, {
        'u8 OAM::GetObjHeight(int shape, int sizeBits)': 'extern "C" u8 _ZN3OAM12GetObjHeightEii(int shape, int sizeBits)',
        'return *(unsigned char *)&((int *)((char *)data_020755ac + sizeBits))[shape];':
            'return *((unsigned char *)data_020755ac + sizeBits + shape * 4);'}),
    ('func_ov002_020f2210', 2, {
        # The same volatile halfword read-modify-write; the packed reference has no -=.
        '*(volatile u16 *)LP(p + 0x12) -= 1;': '*(volatile u16 *)LP(p + 0x12) = *(volatile u16 *)LP(p + 0x12) - 1;'}),
    ('func_ov002_020f2340', 2, {
        # OAM::RenderSub through its mangled ABI symbol.
        'struct OamAttr;\nstruct OAM { static void RenderSub(OamAttr *, int, int); };\nextern OamAttr data_ov002_0210bddc;':
            'extern "C" void _ZN3OAM9RenderSubEP7OamAttrii(void *, int, int);\nextern char data_ov002_0210bddc[];',
        'OAM::RenderSub(&data_ov002_0210bddc,': '_ZN3OAM9RenderSubEP7OamAttrii(data_ov002_0210bddc,'}),
    ('UpdateMinimap', 0, {
        # The stage prelude already names a different MinimapState record.
        'MinimapState': 'UpdateMinimapState'}),
    ('func_ov001_020aaa54', 1, {
        # CapIcon.h's record layout (vptr 0x00 .. mFlags 0x1b); the unit
        # only reads and writes these fields.
        'extern dCapIcon_c* data_ov001_020ad634[];':
            'struct dActor_c;\nstruct dCapIcon_c { void *vptr; struct dActor_c *mOwner; u32 mOwnerUniqueID;\n'
            '    struct dCapIcon_c *mPrev; struct dCapIcon_c *mNext; s32 mSlot;\n'
            '    u8 mCharacter; u8 unk_19; u8 unk_1a; u8 mFlags; };\n'
            'extern dCapIcon_c* data_ov001_020ad634[];\nvoid _ZN10dCapIcon_c6UnlinkEv(dCapIcon_c *);',
        'sl->Unlink();': '_ZN10dCapIcon_c6UnlinkEv(sl);'}),
    ('func_ov001_020aaf40', 1, {
        # C's local false/true enum; C++ already has the keywords.
        'enum { false, true };\n': ''}),
    ('func_020391f0', 0),
    ('func_0206e28c', 0),
    ('func_02023178', 0),
    ('func_02023408', 0),
    ('LoadDebugFont', 0),
    ('func_ov002_020f6bc0', 2),
    ('Vec3_HorzLen', 0),
    ('Vec3_Equal', 0),
    ('func_ov002_020effb8', 2),
    ('func_0200af0c', 0),
    ('func_0202ed14', 0),
    ('func_0202fc40', 0),
    ('func_02059ba0', 0),
    ('func_0201aac8', 0),
    ('func_0201aad4', 0),
    ('func_0201aa18', 0),
    ('func_0201964c', 0),
    ('func_02011a5c', 0),
    ('func_02008b4c', 0),
    ('func_ov002_020f7bb8', 2),
]


PLAYER_SOURCE = [
    ('_ZN6Player9GetHealthEv', 'int Player::GetHealth()'),
]


def mip_fields():
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS)
    fields.update(header_fields('include/dEnemyBase_c.h', 0xd0))
    fields.update(header_fields('include/daMip_c.h', 0x110))
    return fields


MIP_SOURCE = [
    ('_ZN7daMip_c13StateIdleInitEv', 'int daMip_c::StateIdleInit()', {
        'mModelAnim.SetAnim((BCA_File *)(*(void **)((char *)&data_ov085_021305c0 + 4)),':
            '_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0x300,data_ov085_021305c0.filePtr,'}),
]
MIP_DECLARATIONS = '''
extern "C" {
void _ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit(void *,void *,int,int,u16);
}
'''


MINIMAP_UNITS = [
    ('_ZN7Minimap13InitResourcesEv', {
        # The recovered unit under-declares this member call; the ROM passes
        # the same receiver in r0.
        'void _ZN7Minimap19UpdateLevelSpecificEv(void);': 'void _ZN7Minimap19UpdateLevelSpecificEv(void *);',
        '_ZN7Minimap19UpdateLevelSpecificEv();': '_ZN7Minimap19UpdateLevelSpecificEv(this);'}),
    ('_ZN7Minimap19UpdateLevelSpecificEv', {
        'namespace G2S {\n    void* GetBG3CharPtr();\n}\n': '',
        'struct Event {\n    static int GetBit(unsigned int bit);\n};\n': '',
        'int IsStarCollected(int level, int star);':
            'int IsStarCollected(int level, int star);\nvoid *_ZN3G2S13GetBG3CharPtrEv(void);\nint _ZN5Event6GetBitEj(unsigned int);',
        'G2S::GetBG3CharPtr()': '_ZN3G2S13GetBG3CharPtrEv()',
        'Event::GetBit(': '_ZN5Event6GetBitEj('}),
    ('_ZN7Minimap16CleanupResourcesEv', {}),
    ('_ZN7Minimap16OnPendingDestroyEv', {}),
]


MEMBER_UNITS = [
    ('_ZN9Animation8FinishedEv', 0, 'Animation'),
    ('_ZN14CutsceneObject8BehaviorEv', 2, 'CutsceneObject', {
        # The local dBgCh_Gnd probe through its ABI: C1, SetObjAndPos (the
        # reference is a pointer), DetectClsn, clsnY at 0x44, then D1.
        # (Replacements run twice; the anchor is consumed on the first pass.)
        'extern Mtx43 data_020a0e68;\n': 'extern "C" {\n'
            'void *_ZN9dBgCh_GndC1Ev(void *);\nvoid *_ZN9dBgCh_GndD1Ev(void *);\n'
            'void _ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(void *, Vector3 *, void *);\n'
            'int _ZN9dBgCh_Gnd10DetectClsnEv(void *);\n}\nextern Mtx43  data_020a0e68;\n',
        '      dBgCh_Gnd ground;\n      ground.SetObjAndPos(v, 0);\n      if (ground.DetectClsn() != 0)\n      {\n        s32 h = ground.clsnY;':
            '      unsigned char ground[0x50];\n      _ZN9dBgCh_GndC1Ev(ground);\n'
            '      _ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(ground, &v, 0);\n'
            '      if (_ZN9dBgCh_Gnd10DetectClsnEv(ground) != 0)\n      {\n        s32 h = *(s32 *)(ground + 0x44);',
        '          *((u8 *) ((((int) c) + 0x103))) |= 1;\n        }\n      }\n':
            '          *((u8 *) ((((int) c) + 0x103))) |= 1;\n        }\n      }\n      _ZN9dBgCh_GndD1Ev(ground);\n'}),
    ('_ZN7Minimap8BehaviorEv', 2, 'Minimap', {
        # `self` aliases the receiver; spell its members directly.
        '    Minimap *self = this;\n': '', 'self->': '', '(int)self': '(int)this'}),
    # References are pointers in the ARM ABI.
    ('_ZN6Camera8BehaviorEv', 0, 'Camera', {
        'int &dst': 'int *dst', 'short &dst': 'short *dst',
        # The state-table identity test compares a cartridge address.
        '(u32)&data_0208733c': '0x0208733cu',
        '_Z14ApproachLinearRiii(*(int *)(c + 0x134),': '_Z14ApproachLinearRiii((int *)(c + 0x134),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x18c),': '_Z15ApproachLinear2Rsss((short *)(c + 0x18c),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x18e),': '_Z15ApproachLinear2Rsss((short *)(c + 0x18e),',
        '_Z15ApproachLinear2Rsss(*(short *)(c + 0x188),': '_Z15ApproachLinear2Rsss((short *)(c + 0x188),'}),
    # Static Stage::LC_Render: the generated receiver word is unused.
    ('_ZN5Stage9LC_RenderEv', 0, 'Stage', {
        '        RenderBouncingArrows();': '        _ZN5Stage20RenderBouncingArrowsEv();'}),
    ('_ZNK6Camera12IsUnderwaterEv', 0, 'Camera'),
    ('_ZN5Stage9RenderFogEv', 0, 'Stage'),
    ('_ZN5Stage11GetSkyboxIDEv', 0, 'Stage'),
    ('_ZN5Timer10ResetTimerEv', 0, 'Timer'),
    ('_ZN14CutsceneObject13InitResourcesEv', 2, 'CutsceneObject', {
        # The unit names overlay-9 data at 0x02113c20 as a function.
        '&func_02113c20': '&data_ov009_02113c20',
        'int CutsceneObject::InitResources()':
            'extern "C" {\nextern char data_ov009_02113c20;\n}\nint CutsceneObject::InitResources()'}),
    ('_ZN8dActor_c10FindWithIDEj', 0, 'dActor_c'),
]


def class_fields(cls):
    cls = cls.replace('::', '__')
    from stage_actor_views import ACTOR_FIELDS
    fields = dict(ACTOR_FIELDS) if cls.startswith(('d', 'Player')) else {}
    for rel in ('include/'+cls+'.h', 'external/sm64ds-pc/include/'+cls+'.h'):
        if (ROOT/rel).exists():
            fields.update(header_fields(rel, 0))
            break
    return fields


def slices(projected):
    for symbol, overlay, cls, *rest in MEMBER_UNITS:
        row, bindings = isolated(projected, symbol, overlay, cls, class_fields(cls),
                                 replacements=rest[0] if rest else None)
        if not overlay:
            for binding in bindings: binding.pop('overlay', None)
        yield row, bindings
    minimap = header_fields('external/sm64ds-pc/include/Minimap.h', 0x50)
    for symbol, replacements in MINIMAP_UNITS:
        yield isolated(projected, symbol, 2, 'Minimap', minimap, replacements=replacements)
    mip = mip_fields()
    for symbol, name, replacements in MIP_SOURCE:
        yield isolated(projected, symbol, 85, 'daMip_c', mip, replacements=replacements,
                       rel='src/game/actors/d_a_mip.cpp', name=name,
                       types={'DaMipAnimationFilePtr': 'SharedFilePtr', 'DaMipModelFile': 'SharedFilePtr'},
                       prelude=MIP_DECLARATIONS)
    fields = player_fields()
    for symbol in PLAYER_STATES:
        yield isolated(projected, symbol, 2, 'Player', fields)
    for symbol, overlay, *rest in FREE_UNITS:
        row, bindings = free_unit(projected, symbol, overlay, rest[0] if rest else None)
        if not overlay:
            for binding in bindings: binding.pop('overlay', None)
        yield row, bindings
    for symbol, name in PLAYER_SOURCE:
        yield isolated(projected, symbol, 2, 'Player', fields,
                       rel='src/game/player/Player.cpp', name=name)

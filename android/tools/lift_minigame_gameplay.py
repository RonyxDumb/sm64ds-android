"""Project original minigame scene/UI bodies onto checked cartridge records.

Each recovered function has its own unit: the source deliberately gives
several C imports incompatible local views. All cross-unit calls resolve the
original address; no native pointer or C++ vtable enters a four-byte record.
"""
import re
from pathlib import Path
from lift_frontend import Lift, PARSER, DATA_SYMBOLS, normalize

ROOT = Path(__file__).resolve().parents[2]
SYMBOL_FILES = ['config/arm9/symbols.txt', 'config/arm9/itcm/symbols.txt'] + [
    'config/arm9/overlays/'+mod+'/symbols.txt' for mod in ('ov001','ov004','ov006')]
SYMBOLS = {}
for path in SYMBOL_FILES:
    for name, kind, address in re.findall(r'^(\S+) kind:(\w+).*?addr:0x([0-9a-f]+)',
                                         (ROOT/path).read_text(), re.M):
        SYMBOLS[name] = int(address,16)
        if kind != 'function': DATA_SYMBOLS[name] = int(address,16)
DATA_SYMBOLS['sMgBannerFileTemplate']=0x020bc6e8
# The source keeps the explicit receiver used by its C imports; the
# symbol map retains the earlier member spelling for the same entry point.
SYMBOLS['Mg_DrawLine']=SYMBOLS['_ZN11dScMgBase_c11Mg_DrawLineEiiiiii']
SDK_NAMES=('GX','GXS','G2','G2S','IRQ','cstd','Sound','Memory')


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def fields(path, start, end):
    text = (ROOT/path).read_text()
    return clean(text[text.index(start):text.index(end, text.index(start))])


ICON = fields('include/dThIcon_c.h','    s16 mX;', '    dThIcon_c()')
OPTIONS = fields('include/dMgPsOpt_c.h','    TouchIcon_c mIcons[8];','    dMgPsOpt_c();')
BASE = fields('include/dScMgBase_c.h','    s32 mTallyShown;', '\n};')
LUIGI = fields('include/dScMgLuigi_c.h','    dScMgLuigi_Penalty mPenalty[16];','\n};')
AMIDA = fields('include/dScMgAmida_c.h','    s32 mWalkerCell[4][2];','\n};')
AMIDA_PIECE = fields('include/dScMgAmida_c.h','    s32 posX;', '\n};')
BASE_PREFIX = 'u32 vptr,uniqueID,param1; u16 actorID; u8 actorRest[0x42];\n'
COMMON = '''
typedef unsigned char u8; typedef signed char s8; typedef short s16;
typedef unsigned short u16; typedef int s32; typedef unsigned int u32;
typedef long long s64; typedef unsigned long long u64;
typedef volatile u32 vu32; typedef volatile u16 vu16; typedef int Fix12i;
struct Callback { u32 entry; s32 adjustment; };
struct dMgState_c { Callback mEnter,mBehavior,mRender; s32 mState,mTimer,unk_020,mSuspended; };
struct TouchIcon_c { u32 vptr; ICON_FIELDS };
typedef TouchIcon_c dThIcon_c;
typedef void (*Handler)();
struct dMgPsOpt_c { OPTIONS_FIELDS };
struct dScMgBase_c { BASE_PREFIX BASE_FIELDS };
struct dScMgLuigi_c { BASE_PREFIX BASE_FIELDS LUIGI_FIELDS };
struct Point { s32 x,y; };
struct dScMgAmida_c_Piece { AMIDA_PIECE_FIELDS };
struct dScMgAmida_c { BASE_PREFIX BASE_FIELDS AMIDA_FIELDS };
struct MgRecordDefaults { s32 v[5]; };
struct MgEntry { u16 id,pad; s32 sceneParam,scoringMode,scoreCap,ownRecords;
    MgRecordDefaults recordDefaults; s32 musicTrack,musicMode,level; };
struct OamAttr { u16 attr0,attr1,attr2,attr3; };
struct Matrix2x2 { int _00,_01,_10,_11; };
struct GraphCallback { u32 vptr; dScMgBase_c *mScene; };
struct SceneVCall6 { u32 vptr; };
struct Obj { u32 vptr; };
static_assert(sizeof(TouchIcon_c)==0x24);
static_assert(sizeof(dMgPsOpt_c)==0x128);
static_assert(sizeof(dMgState_c)==0x28);
static_assert(sizeof(dScMgBase_c)==0x4660);
static_assert(sizeof(dScMgLuigi_c)==0x545c);
static_assert(sizeof(dScMgAmida_c_Piece)==0x18);
static_assert(sizeof(dScMgAmida_c)==0x53fc);
static_assert(offsetof(dScMgAmida_c,mStrokeGrid)==0x470c);
static_assert(offsetof(dScMgAmida_c,mPieces)==0x4768);
static_assert(sizeof(((dScMgAmida_c *)0)->mWalkerCell[0])==8);
static_assert(sizeof(((dScMgAmida_c *)0)->mLanePos[0])==8);
static_assert(sizeof(((dScMgLuigi_c *)0)->mGrid[0])==9);
static_assert(offsetof(dScMgBase_c,mStateController)==0xcc);
static_assert(offsetof(dScMgBase_c,mTouchOptions)==0xf4);
static_assert(offsetof(dScMgBase_c,mMenuOpen)==0x4628);
static_assert(offsetof(dScMgLuigi_c,mTime)==0x5172);
int mg_NativeVirtual(void *,int,int,int,int,int);
void mg_NativeInvoke(void *,Callback *);
void mg_NativeInvokeSlot(void *,Callback *,int);
int mg_NativeApproach(int *,int,int); int mg_NativeApproachShort(short *,short,short);
int mg_FaderReady(); void mg_SetFaders(void *); void mg_WipeType(void *,int);
void mg_StartScene(int,int,int); void mg_Cache(u32,u32);
void mg_Oam10(bool,OamAttr *,int,int,int,int,int,int,int,int);
OamAttr *mg_Oam8(bool,OamAttr *,int,int,int,int,int,int);
void mg_OamMatrix(bool,OamAttr *,int,int,int,int,Matrix2x2 *);
void mg_OamSub(OamAttr *,int,int,int,int);
'''.replace('ICON_FIELDS',ICON).replace('OPTIONS_FIELDS',OPTIONS).replace('BASE_PREFIX',BASE_PREFIX).replace('BASE_FIELDS',BASE).replace('LUIGI_FIELDS',LUIGI).replace('AMIDA_PIECE_FIELDS',AMIDA_PIECE).replace('AMIDA_FIELDS',AMIDA)

# Complete original member records; retain their cartridge extents.
lh = (ROOT/'include/dScMgLuigi_c.h').read_text()
parts = []
for name in ('Penalty','Curtain','Reward','Iris'):
    tag = 'struct dScMgLuigi_'+name
    first = lh.index(tag)
    parts.append(clean(lh[first:lh.index('\n};',first)+3]))
COMMON = COMMON.replace('struct dScMgLuigi_c', '\n'.join(parts)+'\nstruct dScMgLuigi_c',1)
COMMON += '\n'+clean((ROOT/'include/dMgState_c.h').read_text().split('enum MgSharedState {',1)[1].split('};',1)[0]).join(['enum MgSharedState {','};\n'])
enum_text = lh[lh.index('    enum SceneState'):lh.index('    /* Non-virtual members')]
COMMON += clean(enum_text)
COMMON += '''
#define IME (*(volatile u16 *)0x04000208)
#define REG_DISPCNT (*(volatile u32 *)0x04000000)
#define REG_DISPCNT_SUB (*(volatile u32 *)0x04001000)
#define REG_VCOUNT (*(volatile u16 *)0x04000006)
#define FX_MUL(a,b) ((int)(((s64)(a)*(b)+0x800)>>12))
'''

CLASSES = {'dScMgBase_c':'base','dScMgLuigi_c':'luigi','dScMgAmida_c':'amida','MgFx':'fx',
           'dMgPsOpt_c':'options','dMgPsOpt_c::TouchIcon_c':'option_icon',
           'dThIcon_c':'icon','dScMgBase_c::graphCallback_c':'graph'}
VIRTUALS = {'BeforeInitResources':1,'AfterInitResources':2,'AfterCleanupResources':5,
            'Behavior':6,'BeforeBehavior':7,'Render':9,'BeforeRender':10,
            'OnPendingDestroy':12,'StartRound':18,'CanEndState':19,'OnNewRecord':20,
            'OnDealDone':21,'OnGraphCallback1':22,'OnGraphCallback0':23,
            'OnGraphCallback2':24,'OnGraphCallback3':25,'GetDisplayMode':26,
            'OpenMenu':27,'CloseMenu':28,'SaveScreenForMenu':29,
            'RestoreScreenFromMenu':30,'InitSubScreenBg':31,'InitMainScreenBg':32,
            'InitEngines':33,'PaintSquare':34,'IsAltVariant':35,'IsVariant2':36}
CLASS_FIELDS = {c:set(re.findall(r'\b(?:m\w+|unk_\w+|param1|actorID)\b',
    BASE+LUIGI if c=='dScMgLuigi_c' else BASE+AMIDA if c=='dScMgAmida_c' else BASE if c=='dScMgBase_c' else
    ICON if c.endswith('Icon_c') else OPTIONS if c=='dMgPsOpt_c' else
    'mScene' if c.endswith('graphCallback_c') else 'mPos mVecA mVecB mTimer mDuration mState'))
    for c in CLASSES}
for c in ('dScMgBase_c','dScMgLuigi_c','dScMgAmida_c'): CLASS_FIELDS[c].update(('param1','actorID'))


def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


def function_name(text):
    return re.search(r'([\w:~]+)\s*\(',text)[1]


def symbol_for(qualified, params=''):
    if qualified in SYMBOLS: return qualified
    if '::' in qualified:
        scope,name = qualified.rsplit('::',1)
        prefix = '_ZN'+''.join(str(len(p))+p for p in scope.split('::'))+str(len(name))+name+'E'
        candidates = [s for s in SYMBOLS if s.startswith(prefix)]
        if len(candidates)==1: return candidates[0]
        # SDK overloads are selected explicitly at their call site.
    m = re.fullmatch(r'(?:func_)?(?:ov\d+_)?(0[12][0-9a-f]{6})',qualified)
    if m: return qualified
    raise ValueError('Missing original function address: '+qualified)


def address_of(name):
    if name in SYMBOLS: return SYMBOLS[name]
    if name=='Memory::Allocate': return SYMBOLS['_ZN6Memory8AllocateEj']
    if name=='Memory::Deallocate': return SYMBOLS['_ZN6Memory10DeallocateEPv']
    m = re.search(r'(0[12][0-9a-f]{6})$',name)
    if m: return int(m[1],16)
    return SYMBOLS[symbol_for(name)]


class GameLift(Lift):
    def address(self,name):
        return DATA_SYMBOLS[name] if name in DATA_SYMBOLS else super().address(name)

    @staticmethod
    def array_parts(value):
        dimensions=[]
        while isinstance(value,tuple) and value[0]=='array':
            dimensions.append('['+value[2]+']')
            value=value[1]
        return value,''.join(dimensions)

    def named(self,name,value):
        # decode builds arrays outside-in. Preserve that order when spelling
        # both scene fields and local arrays; [13][9] must retain 9-byte rows.
        if isinstance(value,tuple) and value[0]=='array':
            inner,dimensions=self.array_parts(value)
            return super().named(name,inner)+dimensions
        return super().named(name,value)

    def typename(self,value):
        if isinstance(value,tuple) and value[0]=='array':
            inner,dimensions=self.array_parts(value)
            return super().typename(inner)+dimensions
        return super().typename(value)


class Source:
    def __init__(self,path):
        self.path=path; self.raw=(ROOT/path).read_text(); self.bytes=self.raw.encode()
        self.root=PARSER.parse(self.bytes).root_node
        self.declarations=[]; self.structs=[]; self.functions=[]; self.enums=[]; self.macros=[]
        def visit(n,scope=()):
            if n.type=='function_definition':
                self.functions.append((n,scope)); return
            if n.type in ('struct_specifier','class_specifier') and n.child_by_field_name('body'):
                self.structs.append((n,scope)); return
            if n.type=='namespace_definition':
                scope=scope+(self.text(n.child_by_field_name('name')),)
            if n.type=='declaration': self.declarations.append((n,scope))
            if n.type=='enum_specifier': self.enums.append(n)
            if n.type in ('preproc_def','preproc_function_def'): self.macros.append(n)
            for c in n.named_children: visit(c,scope)
        visit(self.root)
    def text(self,n): return self.bytes[n.start_byte:n.end_byte].decode()
    def tag(self,n):
        tags=re.findall(r'// @symbol (\S+)',self.raw[:n.start_byte])
        return tags[-1] if tags else None


def prototype_names(text):
    probe=Lift(text)
    out=[]
    for n in probe.root.named_children:
        for d in n.children_by_field_name('declarator'):
            if d.type=='init_declarator': d=d.child_by_field_name('declarator')
            try: out.append(probe.decode(d,probe.base(n))[0])
            except ValueError: pass
    return out


def transform(text, cls, own_name, methods):
    text=re.sub(r'\busing\s+[\w:]+\s*;', '',text)
    text=re.sub(r'\busing namespace \w+;', '',text)
    text=re.sub(r'\bs[0-9a-f]{7}::','',text)
    text=text.replace('dMgPsOpt_c::TouchIcon_c','TouchIcon_c').replace('dScMgBase_c::graphCallback_c','GraphCallback')
    if cls: text=re.sub(r'\bself\b','rawSelf',text)
    # The explicit receiver in the signature was introduced by the view.
    if cls: text=text.replace('*rawSelf', '*self',1)
    text=re.sub(r'\bthis\b','self',text)
    if cls:
        h,b=text.split('{',1)
        for field in CLASS_FIELDS[cls]:
            b=re.sub(r'(?<![\w>.])\b'+field+r'\b','self->'+field,b)
        text=h+'{'+b
    if own_name=='mc_luigi_RestartPicture':
        # The retail addressing trick steps a whole scene by a byte index.
        # Access its byte arrays directly instead of forming an unaligned
        # scene object at scene+idx on ARM64.
        for field in ('mShown','mStarted','mKind'):
            text=text.replace('((dScMgLuigi_c *)rawSelf)->'+field+'[0]',
                              'self->'+field+'[idx]')
    if own_name=='mc_luigi_PlaceNextPicture':
        text=text.replace('((dScMgLuigi_c *)((char *)self + col * 9 + row))->mGrid[0]',
                          '((u8 *)self+offsetof(dScMgLuigi_c,mGrid)+col*9+row)')
    # Native service boundaries retain real fader ownership.
    text=text.replace('data_0209f5bc->IsActive()', 'mg_FaderReady()')
    text=text.replace('dScene_c::SetFaders','mg_SetFaders')
    text=text.replace('data_0209f61c.func_0202ec9c(', 'mg_WipeType(&data_0209f61c,')
    text=text.replace('&IME','(volatile u16 *)0x04000208')
    text=text.replace('dScene_c::StartSceneFade','mg_StartScene')
    text=text.replace('CP15::FlushAndInvalidateDataCache','mg_Cache')
    # Reference parameters are adapted explicitly; the stored bytes stay 32-bit.
    text=re.sub(r'\bApproachLinear\(([^,]+),',r'mg_NativeApproach(&\1,',text)
    text=re.sub(r'\bApproachLinear2\(([^,]+),',r'mg_NativeApproachShort(&\1,',text)
    # Two-word original PMFs, never a native member-function pointer.
    text=re.sub(r'\(self->\*([\w]+\[[^\n;]+?\])\)\(([^)]*)\)',
                lambda m:'mg_NativeInvokeSlot(self,&'+m[1]+','+m[2]+')' if m[2] else 'mg_NativeInvoke(self,&'+m[1]+')',text)
    text=text.replace('(self->*(sLuigiCurtainStateTable[self->mCurtain[i].phase]))(i)',
                      'mg_NativeInvokeSlot(self,&sLuigiCurtainStateTable[self->mCurtain[i].phase],i)')
    text=text.replace('(e->*(e->mRender))()', 'mg_NativeInvoke(e,&e->mRender)')
    text=text.replace('(e->*(e->mBehavior))()', 'mg_NativeInvoke(e,&e->mBehavior)')
    text=text.replace('(e->*((ElemFn *)sBannerSetupTable)[st])()', 'mg_NativeInvoke(e,&((ElemFn *)sBannerSetupTable)[st])')
    text=text.replace('if (e->mRender)', 'if (e->mRender.entry || e->mRender.adjustment)')
    text=text.replace('e->mRender != 0', '(e->mRender.entry || e->mRender.adjustment)')
    text=text.replace('e->mBehavior == 0', '(!e->mBehavior.entry && !e->mBehavior.adjustment)')
    text=text.replace('if (((ElemFn *)sBannerSetupTable)[st])', 'if (((ElemFn *)sBannerSetupTable)[st].entry)')
    for field in ('mRender','mBehavior'):
        text=text.replace('e->'+field+' = 0;', 'e->'+field+' = {0,0};')
    text=text.replace('(c->*sMgFxDrawTable[c->mState])()', 'mg_NativeInvoke(c,&sMgFxDrawTable[c->mState])')
    text=text.replace('(c->*sMgFxUpdateTable[c->mState])()', 'mg_NativeInvoke(c,&sMgFxUpdateTable[c->mState])')
    # Rewrite calls using syntax nodes; compound receiver expressions matter.
    data=text.encode(); edits=[]
    for n in walk(PARSER.parse(data).root_node):
        if n.type!='call_expression': continue
        fn=n.child_by_field_name('function'); args=n.child_by_field_name('arguments')
        f=data[fn.start_byte:fn.end_byte].decode()
        a=data[args.start_byte+1:args.end_byte-1].decode()
        replacement=None
        if fn.type=='field_expression':
            obj=fn.child_by_field_name('argument'); field=fn.child_by_field_name('field')
            receiver=data[obj.start_byte:obj.end_byte].decode(); method=data[field.start_byte:field.end_byte].decode()
            op=data[obj.end_byte:field.start_byte].decode().strip()
            if 'mStateController' in receiver:
                replacement='mc_state_'+method+'(&'+receiver+((','+a) if a else '')+')'
            elif 'mTouchOptions' in receiver:
                replacement='mc_options_'+method+'(&'+receiver+((','+a) if a else '')+')'
            elif receiver=='icon' or ('mIcons[' in receiver):
                c='icon' if method=='Behavior' else 'option_icon'
                replacement='mc_'+c+'_'+method+'('+('' if op=='->' else '&')+receiver+((','+a) if a else '')+')'
            elif method in VIRTUALS:
                values=[a] if a else []
                count=len(args.named_children)
                if count>4: raise ValueError('Unexpected scene virtual arity')
                values=([a] if a else [])+['0']*(4-count)
                replacement='mg_NativeVirtual('+('' if op=='->' else '&')+receiver+','+str(VIRTUALS[method])+','+','.join(values)+')'
        elif f in methods.get(cls,{}):
            replacement=methods[cls][f]+'(self'+(','+a if a else '')+')'
        elif f.startswith('dScMgBase_c::'):
            replacement='mc_base_'+f.split('::')[-1]+'((dScMgBase_c *)self'+(','+a if a else '')+')'
        elif f.startswith('dScene_c::'):
            method=f.split('::')[-1]
            replacement='mc_scene_'+method+'((dScMgBase_c *)self'+(','+a if a else '')+')'
        elif f=='OAM::Render':
            count=len(args.named_children)
            replacement={7:'mg_OamMatrix',8:'mg_Oam8',10:'mg_Oam10'}[count]+'('+a+')'
        elif f=='OAM::RenderSub': replacement='mg_OamSub('+a+')'
        if replacement: edits.append((n.start_byte,n.end_byte,replacement))
    # Calls are never nested in these changed member-dispatch expressions.
    end=len(data)
    for first,last,value in sorted(edits,reverse=True):
        if last>end: continue
        data=data[:first]+value.encode()+data[last:]; end=first
    text=data.decode()
    for ns in SDK_NAMES:
        text=text.replace(ns+'::', ns+'_')
    text=text.replace('::OamAttr','OamAttr')
    text=text.replace('__register_global_object','mg_RegisterGlobal')
    text=re.sub(r'\bLuigi_PrepareScanlines\b(?!\s*\()', '(void (*)())0x%08x'%SYMBOLS['Luigi_PrepareScanlines'],text)
    text=re.sub(r'\bNullDestructor_0203d47c\b(?!\s*\()', '(void (*)())0x0203d47c',text)
    text=re.sub(r'\bHALF_AT\((\w+),\s*(\w+)\)',r'(*(short *)((char *)\1+\2*4))',text)
    if cls in ('dMgPsOpt_c','dMgPsOpt_c::TouchIcon_c') or own_name=='PsOpt_PlaceIcon':
        text=re.sub(r'\bmStyle\b','unk_01c',text)
        text=re.sub(r'\bmVariant\b','unk_020',text)
        if cls=='dMgPsOpt_c::TouchIcon_c':
            text=re.sub(r'(?<![\w>.])\b(unk_01c|unk_020)\b',r'self->\1',text)
    # Recovered functions with unused return values ended in a tail-call.
    # Return that actual result, avoiding a native fall-through with UB.
    if own_name in ('mc_base_CloseMenu','mc_base_SaveScreenForMenu','mc_base_InitSubScreenBg','mc_amida_InitSubScreenBg'):
        h,b=text.rsplit('}',1)
        h=h.rstrip()+'\n    return 0; // original callers discard the unspecified return word\n'
        text=h+'}'+b
    text=re.sub(r'\bM (\w+) = \{0\};',r'M \1 = {};',text)
    text=text.replace('x = (x = (e->mX + oamX) + 0x10);','x = (e->mX + oamX) + 0x10;')
    text=re.sub(r'\bPEN\((\w+)\)',r'(((dScMgLuigi_c *)\1)->mPenalty[0])',text)
    text=re.sub(r'TOUCH_REC\((\w+)\)\[([^\]]+)\]\[0\]',r'\1[(\2)*4]',text)
    text=re.sub(r'\b((?:unsigned char|u8) gTouch(?:Held|Edge|X|Y))\[\]\[4\]',r'\1[]',text)
    text=re.sub(r'\b(gTouch(?:Held|Edge|X|Y))\[([^\]]+)\]\[([^\]]+)\]',r'\1[(\2)*4+(\3)]',text)
    text=text.replace('func_02012e1c((char *)self)','func_02012e1c()')
    # The original macros only preserve mwcc addressing choices. Express
    # the same DS field offsets without dereferencing a null native object.
    text=re.sub(r'\b(?:IP|IAP)\((\w+),\s*PIECE_OFF\((\w+)\)\)',r'(((dScMgAmida_c_Piece *)((char *)\1+offsetof(dScMgAmida_c,mPieces)))->\2)',text)
    text=re.sub(r'\bPIECE_OFF\((\w+)\)',r'(offsetof(dScMgAmida_c,mPieces)+offsetof(dScMgAmida_c_Piece,\1))',text)
    text=re.sub(r'\bPIECE_AT\((\w+)\)',r'((dScMgAmida_c_Piece *)((char *)\1+offsetof(dScMgAmida_c,mPieces)))',text)
    text=text.replace('((e->mOamCount + 1) << 2) + 0x1e << 0xc','(((e->mOamCount + 1) << 2) + 0x1e) << 0xc')
    return normalize(text)


def generate(digest, existing):
    paths=['src/minigames/d_s_mg_base.cpp','src/minigames/unit020b2c84.cpp',
           'src/minigames/d_s_mg_luigi.cpp','src/minigames/d_mg_ps_opt.cpp',
           'src/ui/hud/d_th_icon.cpp','src/ui/hud/d_cap_icon.cpp',
           'src/TouchArea_Update.c','src/unnamed/ov001/func_ov001_020ab41c.c',
           'src/minigames/d_s_mg_amida.cpp','src/Vec2_Len.c','src/MultiCopyHalf.c']
    # BG0 and sub BG0..2 already use original stage-source bindings.
    paths += ['src/SetBg'+str(layer)+'Offset.c' for layer in (1,2,3)]
    paths += ['src/SetSubBg3Offset.c']
    paths += ['src/DecIfAbove0_Byte.c']
    paths += ['src/unnamed/arm9/0207/__sinit_02075154.c']
    paths += ['src/unnamed/arm9/0203/func_'+address+'.c' for address in
              ('0203d388','0203d434','0203d480','0203d630','0203d704','0203b958')]
    sources=[Source(p) for p in paths]
    methods={}; definitions=[]; method_definitions=[]; imports={}
    native_leaves={0x020ad660,0x020b04e0,0x020b04ec,0x020b04f0,0x020b1aec,
                   0x020b1b08,0x020b1b40,0x020b1b78,0x020b1ba0}
    for src in sources:
        for node,scope in src.functions:
            head=src.text(node).split('{',1)[0]
            name=function_name(head); cls=None
            if '::' in name:
                cls,method=name.rsplit('::',1)
                if cls not in CLASSES: continue
                if method.startswith('~'): continue
                if method==cls.split('::')[-1]:
                    if cls!='dMgPsOpt_c': continue
                    method='Ctor'
                exported='mc_'+CLASSES[cls]+'_'+method
                methods.setdefault(cls,{})[method]=exported
            else: exported='mg_GameCopyHalf' if name=='MultiCopyHalf' else name
            tag=src.tag(node) or name
            if not tag or tag not in SYMBOLS: continue
            address=SYMBOLS[tag]
            if cls: method_definitions.append((src,node,scope,cls,name,exported,address))
            if address in existing or address in native_leaves: continue
            if not cls and not name.startswith(('Mg_','Banner_','Luigi_','Amida_RestoreBgChars','Amida_SaveBgChars','PsOpt_', 'func_ov001_020ab','TouchArea_Update','SetBg','SetSubBg','Vec2_Len','MultiCopyHalf','func_0203','DecIfAbove0_Byte','Hud_RenderSprite','RenderOamMainScreen','DrawOamSprite','RenderOamBothScreens','FreeGfxSlotsById','__sinit_02075154')): continue
            if src.path.endswith('d_cap_icon.cpp') and name not in ('func_ov001_020ab3f0','func_ov001_020ab41c','TouchArea_Update'): continue
            definitions.append((src,node,scope,cls,name,exported,address))
    # Method imports use the recovered mangled address, with a POD self.
    for src,node,scope,cls,name,exported,address in method_definitions:
        if not cls: continue
        head=clean(src.text(node).split('{',1)[0]).strip()
        if cls=='dMgPsOpt_c' and name=='dMgPsOpt_c::dMgPsOpt_c': head='void '+head
        head=head.replace(name,exported,1)
        record='TouchIcon_c' if cls.endswith('Icon_c') else 'GraphCallback' if cls.endswith('graphCallback_c') else cls
        head=head.replace('(', '('+record+' *self'+(',' if head.split('(',1)[1].strip() not in (')','void)') else ''),1)
        head=head.replace('selfvoid','self')
        imports[exported]=(head+';',address)
    for method,addr in [('Behavior',0x020b8778),('Render',0x020b8714),('SetState',0x020b87e0)]:
        imports['mc_state_'+method]=('void mc_state_'+method+'(dMgState_c *'+(',int' if method=='SetState' else '')+');',addr)
    for method in ('BeforeInitResources','BeforeBehavior','BeforeRender','AfterInitResources','AfterCleanupResources'):
        addr=address_of(symbol_for('dScene_c::'+method))
        imports['mc_scene_'+method]=(('void' if method.startswith('After') else 'int')+' mc_scene_'+method+'(dScMgBase_c *'+(',u32' if method.startswith('After') else '')+');',addr)
    # Local views needed by imported declarations, with original byte layout.
    records=[]
    for src in sources:
        for node,scope in src.structs:
            tag=node.child_by_field_name('name')
            if tag is None:
                if node.parent.type!='type_definition':
                    raise ValueError('Unnamed source record without typedef: '+src.path)
                name=src.text(node.parent.child_by_field_name('declarator'))
                text=clean(src.text(node.parent))
                if name not in [n for n,_ in records]: records.append((name,text))
                continue
            name=src.text(tag)
            if name in ('MgFx','SceneVCall6','Obj','MgBanner','OamAttr','Matrix2x2'):
                continue
            text=clean(src.text(node))+';'
            if name=='D3DBaseView':
                text=text.replace(': dScMgBase_c','').replace('{','{'+BASE_PREFIX+BASE,1)
            if 'virtual ' in text: continue
            if name not in [n for n,_ in records]: records.append((name,text))
    fx=clean(sources[0].raw.split('struct MgFx {',1)[1].split('\n};',1)[0])
    fx=fx[:fx.index('    MgFx()')]
    records.append(('MgFx','struct MgFx {'+fx+'};'))
    banner=sources[1].raw.split('struct MgBanner {',1)[1].split('\n};',1)[0]
    banner=clean(banner[:banner.index('    MgBanner();')]).replace('ElemFn','Callback')
    records.append(('MgBanner','struct MgBanner {'+banner+'};'))
    common=COMMON+'\ntypedef Callback ElemFn; typedef Callback EntPMF; typedef Callback LuigiSlotHandler; typedef Callback LuigiStateHandler;\n'
    common+='\n'.join(t for _,t in records)+'\nstatic_assert(sizeof(MgBanner)==0x134);\n'
    # Extra source enums/macros carry no storage and are part of the reviewed view.
    common+='\n'.join(clean(s.text(n))+';' for s in sources for n in s.enums)
    shared_headers=[Source(p) for p in ('include/decl_common.h','include/PlayerInput.h','include/Sound.h','include/IRQ.h')]
    declarations=[]
    for src in sources+shared_headers:
        for n,scope in src.declarations:
            t=clean(src.text(n)).strip()
            if not t or t.startswith(('static ','typedef ')): continue
            names=prototype_names(t)
            if scope and scope[-1] in SDK_NAMES:
                for name in names:
                    t=re.sub(r'\b'+name+r'\b',scope[-1]+'_'+name,t)
                names=[scope[-1]+'_'+name for name in names]
            # Stored callback arrays come from the cartridge's static data,
            # not host C++ member-pointer initializers.
            if '=' in t:
                t='extern '+t.split('=',1)[0].strip()+';'
            declarations.append((src,n,scope,t,names))
    projected_common=GameLift(common).result()
    entries=[{'path':'include/dScMgBase_c.h','sha256':digest('include/dScMgBase_c.h'),
              'header':True,'dependencies':{p:digest(p) for p in
              ('include/dScMgLuigi_c.h','include/dMgState_c.h','include/dMgPsOpt_c.h',
               'include/dThIcon_c.h','include/dScMgAmida_c.h')},'source':projected_common}]
    bindings=[]
    for src,node,scope,cls,name,exported,address in definitions:
        text=clean(src.text(node))
        if not cls and exported!=name: text=text.replace(name,exported,1)
        if cls=='dMgPsOpt_c' and name=='dMgPsOpt_c::dMgPsOpt_c': text='void '+text
        if cls:
            text=text.replace(name,exported,1)
            record='TouchIcon_c' if cls.endswith('Icon_c') else 'GraphCallback' if cls.endswith('graphCallback_c') else cls
            h,b=text.split('{',1)
            args=h.split('(',1)[1].rsplit(')',1)[0].strip()
            args='' if args=='void' else args
            h=h.split('(',1)[0]+'('+record+' *self'+(','+args if args else '')+') '
            text=h+'{'+b
        text=transform(text,cls,exported,methods)
        # Null original member-pointer constants have the exact two-word form.
        if name=='Mg_DrawFx':
            text=text.replace('(e->*(sMgFxDrawTable[e->mState]))()', 'mg_NativeInvoke(e,&sMgFxDrawTable[e->mState])')
        if name=='Mg_UpdateFx':
            text=text.replace('(e->*(sMgFxUpdateTable[e->mState]))()', 'mg_NativeInvoke(e,&sMgFxUpdateTable[e->mState])')
        words=set(re.findall(r'\b\w+\b',text))
        chosen={}; local_imports=dict(imports)
        # Most-local preceding declaration wins; the explicit namespace views
        # of out-of-namespace method definitions are selected by their using.
        used_ns=set(re.findall(r'using (\w+)::',src.text(node)))
        used_ns.update(re.findall(r'\b(s[0-9a-f]{7})::',src.text(node)))
        used_ns.update(re.findall(r'using namespace (\w+)',src.text(node)))
        candidates=sorted(declarations,key=lambda d:(d[0] is src,d[2] in (scope,),bool(d[2] and d[2][-1] in used_ns),d[1].start_byte<node.start_byte,d[1].start_byte))
        for owner,n,ns,t,names in candidates:
            if owner is src and ns and ns!=scope and ns[-1] not in used_ns and ns[-1] not in SDK_NAMES: continue
            for declared in names:
                if declared in words: chosen[declared]=t
        for declared in sorted(words & imports.keys()): chosen[declared]=imports[declared][0]
        chosen.pop(exported,None)
        for n in PARSER.parse(COMMON.encode()).root_node.named_children:
            if n.type=='declaration':
                t=COMMON.encode()[n.start_byte:n.end_byte].decode()
                if set(prototype_names(t)) & words: chosen['service_'+str(n.start_byte)]=t
        if 'mg_RegisterGlobal' in words: chosen['mg_RegisterGlobal']='void mg_RegisterGlobal(void *,void *,void *);'
        # Macros from the input header are explicit byte aliases in this view.
        for touch in ('gTouchHeld','gTouchEdge','gTouchX','gTouchY'):
            if touch in words: chosen[touch]='extern unsigned char '+touch+'[];'
        if 'gActivePlayerSlot' in words: chosen['gActivePlayerSlot']='extern unsigned char gActivePlayerSlot;'
        # Global references declared in a matching header must also remain live.
        for declared in sorted(words & DATA_SYMBOLS.keys()):
            if declared not in chosen:
                if declared=='gMgScene': chosen[declared]='extern dScMgBase_c *gMgScene;'
                elif declared=='gMgMenuRow': chosen[declared]='extern MgEntry gMgMenuRow;'
        chosen['gMgScene']='extern dScMgBase_c *gMgScene;'
        if 'sMgGraphCallback' in words: chosen['sMgGraphCallback']='extern GraphCallback sMgGraphCallback;'
        if 'sMgFxPool' in words: chosen['sMgFxPool']='extern MgFx sMgFxPool[];'
        if 'sMgBannerFileTemplate' in words: chosen['sMgBannerFileTemplate']='extern EntryTable sMgBannerFileTemplate;'
        for declared in ('data_0209f61c',):
            if declared in words: chosen[declared]='extern u8 '+declared+';'
        for local in re.findall(r'\bextern [^;{}]+;',text):
            for declared in prototype_names(local): chosen.pop(declared,None)
        # Inline numeric helpers, originally emitted at their callers.
        helpers='''
static int FX_Mul(int a,int b) { return (int)(((s64)a*b+0x800)>>12); }
static s16 FX_SinIdx(int idx) { return data_02082214[idx*2]; }
static s16 FX_CosIdx(int idx) { return data_02082214[idx*2+1]; }
static void MTX_Rot22(M *p,int s,int c) { p->_00=c;p->_01=s;p->_10=-s;p->_11=c; }
static long long inline_fn(int a) { return (long long)a; }
static int sdiv2(int a) { return a/2; }
static int sdiv4(int a) { return a/4; }
'''
        needed_helpers=''
        for line in helpers.splitlines():
            if line and function_name(line) in words: needed_helpers+=line+'\n'
        if 'FX_SinIdx' in words or 'FX_CosIdx' in words: chosen['data_02082214']='extern s16 data_02082214[];'
        pre='\n'.join(dict.fromkeys(transform(t,None,'',methods) for t in chosen.values()))
        # Namespace-static SDK functions are given C import spellings.
        text=pre+'\n'+needed_helpers+text
        try:
            lifted=GameLift(text).result()
        except Exception as exc:
            raise ValueError('Projection failed for '+name+': '+str(exc)) from exc
        if exported=='mc_amida_ProbeRung':
            # The original nontrivial Point travels via a caller-owned
            # copy on ARM. Preserve that boundary and C++ value semantics.
            for i in (1,2): lifted=lifted.replace('(Point)(a%d)'%i,'*Ptr<Point>(a%d)'%i)
        # Turn all imports into local typed calls through the original address.
        protected='pk_'+exported
        seen=set()
        for match in list(re.finditer(r'extern "C" ([^;{}\n]+?) (pk_\w+)\(([^;{}]*)\);',lifted))[::-1]:
            ret,callee,params=match.groups(); original=callee.removeprefix('pk_')
            if callee==protected:
                lifted=lifted[:match.start()]+lifted[match.end():]; continue
            if original.startswith('mg_'): continue
            if callee in seen:
                lifted=lifted[:match.start()]+lifted[match.end():]; continue
            seen.add(callee)
            if original in local_imports: target=local_imports[original][1]
            else:
                q=original
                for ns in SDK_NAMES:
                    if q not in SYMBOLS and q.startswith(ns+'_'): q=ns+'::'+q[len(ns)+1:]; break
                target=address_of(q)
            probe=Lift('void probe('+params+');')
            decl=probe.root.named_children[0].child_by_field_name('declarator')
            types=[]
            for p in decl.child_by_field_name('parameters').named_children:
                if p.type=='parameter_declaration':
                    _,typ=probe.decode(p.child_by_field_name('declarator'),probe.base(p))
                    typ=probe.typename(typ)
                    if typ!='void': types.append(typ)
            args=', '.join(t+' a'+str(i) for i,t in enumerate(types))
            abi_types=['Ptr<Point>' if t=='Point' else t for t in types]
            call='Ptr<'+ret+'('+', '.join(abi_types)+')>(0x%08xu)'%target
            call+='('+', '.join('Ptr<Point>(&a%d)'%i if t=='Point' else 'a'+str(i) for i,t in enumerate(types))+');'
            service={0x02011b7c:'mg_GameStopEffects',0x0201200c:'mg_GameLoadGroup',
                     0x0202e638:'mg_GameBeforeInit',0x02018100:'mg_GameCompressed'}.get(target)
            extra=''
            if service:
                extra='extern "C" '+ret+' pk_'+service+'('+', '.join(types)+');\n'
                call='pk_'+service+'('+', '.join('a'+str(i) for i in range(len(types)))+');'
            lifted=lifted[:match.start()]+extra+'[[maybe_unused]] static '+ret+' '+callee+'('+args+') { '+('' if ret=='void' else 'return ')+call+' }'+lifted[match.end():]
        lifted='#include "dScMgBase_c.h"\n'+lifted
        entries.append({'path':src.path,'unit':'mg_%08x'%address,'sha256':digest(src.path),
                        'header':False,'dependencies':{p:digest(p) for p in
                        ('include/dScMgBase_c.h','include/dScMgLuigi_c.h','include/dMgState_c.h',
                         'include/dMgPsOpt_c.h','include/dThIcon_c.h','include/dScMgAmida_c.h','include/decl_common.h')},'source':lifted})
        bindings.append({'address':address,'symbol':'abi_'+exported,'overlay':6 if src.path.endswith(('d_s_mg_luigi.cpp','d_s_mg_amida.cpp')) else 4})
    # These original __sinit arrays live in BSS. Loading the overlay bytes
    # alone leaves zero callbacks: reproduce the source initializers exactly.
    init=['void mg_InitNativeStatics() {']
    tables=('sMgFxUpdateTable','sMgFxDrawTable','sBannerSetupTable',
            'sBannerUpdateTable','sBannerDrawTable','sLuigiIrisTable',
            'sLuigiCurtainTable','sLuigiCurtainStateTable','sLuigiStateTable',
            'sLuigiSlotHandlers')
    for table in tables:
        found=[]
        for src in sources:
            found += re.findall(r'\b'+table+r'\[\w*\]\s*=\s*\{([^}]+)\}',clean(src.raw))
        if len(found)!=1: raise ValueError('Original callback table changed: '+table)
        for index,value in enumerate(found[0].split(',')):
            value=value.strip()
            if not value: continue
            target=SYMBOLS[table]+index*8
            if value.startswith('&'):
                callback=address_of(value[1:])
                init.append('*(u32 *)0x%08xu=0x%08xu; *(s32 *)0x%08xu=0;'%(target,callback,target+4))
            else:
                init.append('*(Callback *)0x%08xu=*(Callback *)0x%08xu;'%(target,DATA_SYMBOLS[value]))
    for i in range(3): init.append('*(s32 *)0x%08xu=29;'%(SYMBOLS['gMgBanners']+i*0x134+0x20))
    init.append('*(u32 *)0x%08xu=0x%08xu;'%(SYMBOLS['sMgGraphCallback'],SYMBOLS['_ZTVN11dScMgBase_c15graphCallback_cE']+8))
    init.append('}')
    entries.append({'path':paths[0],'unit':'mg_static_init','sha256':digest(paths[0]),
                    'header':False,'dependencies':{p:digest(p) for p in paths[1:3]},
                    'source':'#include "dScMgBase_c.h"\n'+Lift('\n'.join(init)).result()})
    return entries,bindings

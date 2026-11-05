"""Exact 0x28-byte views of the recovered shared minigame controller.

Member pointers retain the cartridge's two-word representation. Only their
native dispatch and the polymorphic fader/scene boundary are adapted.
"""
import re
from pathlib import Path
from lift_frontend import Lift, PARSER, DATA_SYMBOLS
from native_sources import replace_once

ROOT = Path(__file__).resolve().parents[2]
FIELDS = ('mEnter', 'mBehavior', 'mRender', 'mState', 'mTimer', 'unk_020', 'mSuspended')
TYPES = '''
typedef unsigned char u8; typedef short s16; typedef unsigned short u16;
typedef int s32; typedef unsigned int u32;
struct Callback { u32 entry; s32 adjustment; };
struct dMgState_c { Callback mEnter, mBehavior, mRender;
    s32 mState, mTimer, unk_020, mSuspended; };
struct Base { u8 base[0xa8]; s32 mCredits; };
static_assert(sizeof(Callback)==8);
static_assert(sizeof(dMgState_c)==0x28);
static_assert(offsetof(dMgState_c,mState)==0x18);
static_assert(offsetof(dMgState_c,mSuspended)==0x24);
static_assert(offsetof(Base,mCredits)==0xa8);
'''
SERVICES = '''
void mg_StateInvoke(void *, Callback *);
int mg_StateSceneCall(void *, int, int);
int mg_StateFaderAtStart(); int mg_StateFaderAtEnd(); int mg_StateFaderBetween();
int mg_StateFaderForward(unsigned int,int); int mg_StateFaderBackward(unsigned int,int);
int mg_StateApproach(int *,int,int);
void mg_StateValidate(int);
void mg_SetFaders(void *); void mg_WipeType(void *,int); void mg_StartScene(int,int,int);
'''


def generate(digest):
    rel = 'src/minigames/d_mg_state.cpp'
    raw = (ROOT/rel).read_text()
    # These data names follow their original address order, confirmed against
    # the EU reset's literal loads and banner calls. They are live overlay data.
    layout = re.findall(r'extern int (sMg\w+);', raw[raw.index('extern int sMgScoreX;'):raw.index('extern unsigned char *sMgStateText')])
    if len(layout)!=27:
        raise ValueError('shared minigame banner layout changed')
    data = {name:0x020bc850+i*4 for i,name in enumerate(layout)}
    data.update({'gMgScene':0x020beb68, 'sMgTopScreenYOffset':0x020beb6c,
                 'sMgBannerButtonsArmed':0x020bc150, 'gMgCoinsShowDelta':0x020bc7d4,
                 'sMgCoinsDeltaShown':0x020bfa24, 'sMgStateTextSpritesByLanguage':0x020bca44,
                 'gActivePlayerSlot':0x020a0e40,'gTouchHeld':0x020a0de8,'gTouchEdge':0x020a0de9})
    behavior = re.findall(r'extern dMgState_c::Callback (pmf_\w+);',raw)
    if len(behavior)!=49:
        raise ValueError('shared minigame member pointer set changed')
    # The first 16 records are behavior/render callbacks, then the source's
    # declaration order interleaves enter handlers and their shared callbacks.
    pmf_order = '''NextRound_Update RoundBanner5_Update DrawScore RetryPrompt_Update
UpdateRetryFadeOut Results_Update Win_Update Lose_Update UpdateRetryPrompt
UpdateGameOverToMenu LoseResume_Update UpdateRetryFadeOut_2 DrawRecordTable
GameOver_Update Draw_Enter Tie_Enter UpdateAfterHidden LoseResume_Enter
Lose_Enter Win_Enter RetryPrompt_Enter RoundBanner5_Enter NextRound_Enter
RoundClear_Enter RoundClearTop_Enter RetryPromptB_Enter UpdateGameOverToMenu_2
UpdateFadeThenResume ScoreThenRound_Enter RetryPromptWide_Enter
UpdateWaitThenResume UpdateWaitThenResume_2 DigitsThenRound_Enter DrawScoreDigits
ResultsNewRecord_Update BannerEThenRound_Enter Results_Enter RoundClearTop_Update
ResultsNewRecord_Enter GameOver_Enter RetryPromptB_Update UpdateFadeThenResume_2
Finished_Enter UpdateRetryFadeOut_3 RoundClear_Update ScoreThenRound_Update
DrawRecordTable_2 Finished_Update RetryPromptWideB_Enter'''.split()
    if {'pmf_dMgState_c_'+name for name in pmf_order}!=set(behavior):
        raise ValueError('shared minigame member pointer order changed')
    data.update({'pmf_dMgState_c_'+name:0x020bc8bc+i*8 for i,name in enumerate(pmf_order)})
    DATA_SYMBOLS.update(data)
    addresses={name:int(address,16) for address,name in re.findall(r'\*\s+(0x[0-9a-f]+)\s+(\w+)',raw)}
    addresses.update({'Ctor':0x020b8a70,'SetState':0x020b87e0,
                      'Behavior':0x020b8778,'Render':0x020b8714})
    methods=[]
    def visit(node):
        if node.type=='function_definition':
            methods.append(raw.encode()[node.start_byte:node.end_byte].decode())
            return
        for child in node.named_children: visit(child)
    visit(PARSER.parse(raw.encode()).root_node)
    body=[]; declarations=[]; bindings=[]
    for text in methods:
        head=text.split('{',1)[0]
        if 'dMgState_c::dMgState_c()' in head:
            text='void mg_StateCtor(dMgState_c *self) { self->mState=-1; self->mTimer=0; self->unk_020=0; self->mSuspended=0; }'
            name='Ctor'; exported='mg_StateCtor'
        elif re.search(r'dMgState_c\s*::',head):
            name=re.search(r'dMgState_c\s*::(\w+)',head)[1]
            exported='mg_State'+name
            args='dMgState_c *self'+(', s32 state' if name=='SetState' else '')
            text=re.sub(r'dMgState_c\s*::\w+\([^)]*\)',exported+'('+args+')',text,count=1)
            text=re.sub(r'\bthis\b','self',text)
            h,b=text.split('{',1)
            for field in FIELDS:
                b=re.sub(r'(?<![\w>.])\b'+field+r'\b','self->'+field,b)
            text=h+'{'+b
        else:
            name=re.search(r'void (\w+)\(',head)[1]
            exported=name
        if name not in addresses:
            raise ValueError('unknown shared minigame function: '+name)
        for method in re.findall(r'void dMgState_c\s*::(\w+)\(',raw):
            text=re.sub(r'(?<![\w>:])\b'+method+r'\(\)', 'mg_State'+method+'(self)',text)
        text=text.replace('static Callback sEnterTable[20]', 'Callback sEnterTable[20]')
        if name=='SetState':
            text=replace_once(text,'    self->mState = state;',
                              '    mg_StateValidate(state);\n    self->mState = state;')
        text=text.replace('self->mRender = 0;', 'self->mRender = data_02086b58;')
        text=text.replace('*(W2 *)&self->mRender = data_02086b58;', 'self->mRender = data_02086b58;')
        text=re.sub(r'if \((self->m(?:Enter|Behavior|Render)) == 0\)',r'if (!\1.entry && !\1.adjustment)',text)
        text=text.replace('if (self->mEnter)', 'if (self->mEnter.entry || self->mEnter.adjustment)')
        text=re.sub(r'\(self->\*(self->m\w+)\)\(\)',r'mg_StateInvoke(self, &\1)',text)
        for obj in ('scene','g','q'):
            text=text.replace(obj+'->CanEndState(self->mState)', 'mg_StateSceneCall('+obj+',19,self->mState)')
        text=text.replace('scene->OnNewRecord()', 'mg_StateSceneCall(scene,20,0)')
        text=text.replace('dFdBrightness_c *fader;', 'void *fader;')
        text=text.replace('fader = data_0209f5bc;', 'fader = 0;')
        for obj in ('data_0209f5bc','fader'):
            for old,new in [('IsAtStart','mg_StateFaderAtStart'),('IsAtEnd','mg_StateFaderAtEnd'),('IsBetweenStartAndEnd','mg_StateFaderBetween')]:
                text=text.replace(obj+'->'+old+'()', new+'()')
            for old,new in [('SetForwardTime','mg_StateFaderForward'),('SetBackwardTime','mg_StateFaderBackward')]:
                text=text.replace('((FaderCallView *)'+obj+')->'+old, new)
        text=text.replace('dScene_c::SetFaders','mg_SetFaders').replace('data_0209f61c.func_0202ec9c(2)','mg_WipeType(&data_0209f61c,2)')
        text=text.replace('dScene_c::StartSceneFade','mg_StartScene')
        text=text.replace('Sound::PlayBank2_2D','func_02012790')
        text=text.replace('ApproachLinear(self->mTimer,','mg_StateApproach(&self->mTimer,')
        body.append(text)
        declarations.append('void '+exported+'('+('dMgState_c *'+(',s32' if name=='SetState' else '') if 'mg_State' in exported else ('int' if name=='Mg_SetNextBanner' else ''))+');')
        bindings.append({'address':addresses[name],'symbol':'abi_'+exported})
    globals='extern Base *gMgScene; extern Callback data_02086b58; extern u8 data_0209f61c;\n'
    globals+='extern u8 gActivePlayerSlot,gTouchHeld[],gTouchEdge[];\n'
    globals+='\n'.join('extern int '+name+';' for name in data if not name.startswith('pmf_') and name not in ('gMgScene','sMgStateTextSpritesByLanguage','gActivePlayerSlot','gTouchHeld','gTouchEdge'))
    globals+='\nextern u8 *sMgStateTextSpritesByLanguage[];\n'
    globals+='\n'.join('extern Callback '+name+';' for name in behavior)
    # Keep original imports typed; resolving a missing body is an error at
    # the call site, never a successful fake banner/audio/score operation.
    imports=raw[raw.index('extern "C" {'):raw.index('struct W2')]
    imports=imports[:imports.rfind('}')].removeprefix('extern "C" {')
    imports=imports.replace('extern void Mg_SetNextBanner(int);','')
    text=TYPES+globals+'\n'+SERVICES+imports+'\n'+'\n'.join(declarations)+'\n'+'\n'.join(body)
    class StateLift(Lift):
        def address(self,name):
            return data[name] if name in data else super().address(name)
    lifted=StateLift(text).result()
    # Original imports use addresses recovered with the containing namespace;
    # the code bindings are exact and are checked before any native call.
    symbols={}
    for p in ('config/arm9/symbols.txt','config/arm9/itcm/symbols.txt'):
        for n,a in re.findall(r'^(\S+) kind:function.*?addr:0x([0-9a-f]+)',(ROOT/p).read_text(),re.M):
            symbols[n]=int(a,16)
    for p in ('src/minigames/d_s_mg_base.cpp','src/minigames/unit020b2c84.cpp'):
        source=(ROOT/p).read_text()
        for tag,addr in re.findall(r'// @symbol (\w+)\s*\nnamespace s([0-9a-f]+)',source):
            symbols[tag]=int(addr,16)
    protected={b['symbol'].removeprefix('abi_') for b in bindings}
    for match in list(re.finditer(r'extern "C" ([^;{}\n]+?) (pk_\w+)\(([^;{}]*)\);',lifted))[::-1]:
        ret,name,params=match.groups(); original=name.removeprefix('pk_')
        if original in protected or original.startswith('mg_'): continue
        if original not in symbols: raise ValueError('unresolved shared minigame import '+original)
        probe=Lift('void probe('+params+');')
        decl=probe.root.named_children[0].child_by_field_name('declarator')
        types=[]
        for p in decl.child_by_field_name('parameters').named_children:
            if p.type=='parameter_declaration':
                _,typ=probe.decode(p.child_by_field_name('declarator'),probe.base(p))
                typ=probe.typename(typ)
                if typ!='void': types.append(typ)
        args=', '.join(t+' a'+str(i) for i,t in enumerate(types))
        call='Ptr<'+ret+'('+', '.join(types)+')>(0x%08xu)'%symbols[original]
        call+='('+', '.join('a'+str(i) for i in range(len(types)))+');'
        replacement='static '+ret+' '+name+'('+args+') { '+('' if ret=='void' else 'return ')+call+' }'
        lifted=lifted[:match.start()]+replacement+lifted[match.end():]
    row={'path':rel,'sha256':digest(rel),'header':False,'dependencies':{
        'include/dMgState_c.h':digest('include/dMgState_c.h')},'source':lifted}
    return row,bindings,data

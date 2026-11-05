"""Recover the shared scene's original score/ranking bodies without a vptr ABI."""
import re
from pathlib import Path
from lift_frontend import Lift, PARSER, DATA_SYMBOLS

ROOT=Path(__file__).resolve().parents[2]
TYPES='''
typedef unsigned char u8; typedef unsigned int u32; typedef int s32;
struct MgRecordDefaults { s32 v[5]; };
struct MgEntry { unsigned short id,pad; s32 sceneParam,scoringMode,scoreCap,ownRecords;
    MgRecordDefaults recordDefaults; s32 musicTrack,musicMode,level; };
struct dScMgBase_c {
    u8 base[8]; u32 param1; u8 pad00c[0x58]; s32 mLastRecordRank;
    u8 pad068[0x40]; s32 mCredits; u8 pad0ac[0x1c]; s32 mMusicTrack;
    u8 pad0cc[0x4580]; s32 mSubmittedScore,mTopRecord,mSessionBest,mRankedScore;
    u8 mNewRecord,pad465d[3];
};
static_assert(sizeof(MgEntry)==0x34);
static_assert(sizeof(dScMgBase_c)==0x4660);
static_assert(offsetof(dScMgBase_c,param1)==8);
static_assert(offsetof(dScMgBase_c,mLastRecordRank)==0x64);
static_assert(offsetof(dScMgBase_c,mMusicTrack)==0xc8);
static_assert(offsetof(dScMgBase_c,mSubmittedScore)==0x464c);
static_assert(offsetof(dScMgBase_c,mNewRecord)==0x465c);
extern dScMgBase_c *gMgScene;
extern MgEntry gMgMenuRow;
extern u8 gMgSaveDirty[];
extern int sMgTopScreenYOffset[];
int mg_GetRecord(int,int); void mg_SetRecord(int,int,int); int mg_Language();
int mg_StateSceneCall(void *,int,int);
'''
PROTOTYPES='''
int GetGameLanguage(); int Mg_GetLastRecordRank(dScMgBase_c *);
int Mg_FindRecordRank(void *,int); void Mg_SaveRecordPair(int,int);
int Mg_GetSavedRecord1(); int Mg_GetSavedRecord0(); void Mg_CommitRecords();
void Mg_RankSessionBest(dScMgBase_c *); void Mg_ClearNewRecordFlag();
void Mg_SetSessionBest(int); int Mg_GetSessionBest(); void Mg_SubmitScore(int);
int Mg_GetSubmittedScore(); int Mg_IsNewRecord(); void Mg_SetTopRecord(int);
int Mg_GetTopRecord(); int Mg_GetSaveTable(void *); int Mg_GetScoringMode();
int Mg_GetMusicMode(); int Mg_GetMusicTrack(); int Mg_GetTopScreenYOffset();
void Mg_SetTopScreenYOffset(int); void Mg_CallStartRound(dScMgBase_c *,void *);
'''


def generate(digest):
    rel='src/minigames/d_s_mg_base.cpp'
    raw=(ROOT/rel).read_text(); source=raw.encode()
    selected=set(re.findall(r'\b((?:Mg_|GetGameLanguage)\w*)\(',PROTOTYPES))
    bodies={}; addresses={}
    def visit(node):
        if node.type=='function_definition':
            text=source[node.start_byte:node.end_byte].decode()
            head=text.split('{',1)[0]
            found=re.search(r'\b(\w+)\([^)]*\)\s*$',head)
            if found and found[1] in selected:
                name=found[1]; bodies[name]=text
                namespaces=re.findall(r'namespace s([0-9a-f]+)',raw[:node.start_byte])
                addresses[name]=int(namespaces[-1],16)
            return
        for child in node.named_children: visit(child)
    visit(PARSER.parse(source).root_node)
    if set(bodies)!=selected:
        raise ValueError('original minigame record functions changed: '+str(selected-set(bodies)))
    body='\n'.join(bodies.values())
    body=body.replace('func_02013580','mg_GetRecord').replace('func_02013568','mg_SetRecord')
    body=body.replace('GetOwnerLanguage()','mg_Language()')
    body=body.replace('scene->StartRound((int)state);','mg_StateSceneCall(scene,18,(int)state);')
    data={'gMgScene':0x020beb68,'gMgMenuRow':0x0209b308,
          'gMgSaveDirty':0x0209b300,'sMgTopScreenYOffset':0x020beb6c}
    DATA_SYMBOLS.update(data)
    class RecordLift(Lift):
        def address(self,name):
            return data[name] if name in data else super().address(name)
    projected=RecordLift(TYPES+PROTOTYPES+body).result()
    return {'path':rel,'sha256':digest(rel),'header':False,
            'dependencies':{'include/dScMgBase_c.h':digest('include/dScMgBase_c.h'),
                            'include/MgMenuRow.h':digest('include/MgMenuRow.h')},
            'source':projected},[{'address':addresses[name],'symbol':'abi_'+name} for name in bodies]

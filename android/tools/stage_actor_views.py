"""Reviewed cartridge-layout views of complete castle actor methods.

Only receivers, reference ABI and field addresses change. The source bodies,
branches, asset selection and failure returns remain the game's own.
"""
import re
from pathlib import Path
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]
ACTOR_FIELDS = {
    'uniqueID': ('u32', 4), 'param1': ('u32', 8),
    'mPosX': ('s32', 0x5c), 'mPosY': ('s32', 0x60), 'mPosZ': ('s32', 0x64),
    'mScaleX': ('s32', 0x80), 'mScaleY': ('s32', 0x84), 'mScaleZ': ('s32', 0x88),
    'mAngleX': ('s16', 0x8c), 'mAngleY': ('s16', 0x8e), 'mAngleZ': ('s16', 0x90),
    'mPrevAngleY': ('s16', 0x94), 'mHorzSpeed': ('s32', 0x98),
    'mVertAccel': ('s32', 0x9c), 'mTerminalVelocity': ('s32', 0xa0),
    'mSpeedX': ('s32', 0xa4), 'mVertSpeed': ('s32', 0xa8), 'mSpeedZ': ('s32', 0xac),
    'mFlags': ('u32', 0xb0), 'mAreaId': ('s8', 0xcc),
}
MODEL_API = '''
void *_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
void *_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
int _ZN11dExtModel_c7SetFileEP8BMD_Fileii(void *,void *,int,int);
void _ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit(void *,void *,int,int,u16);
int _ZN17dExtShadowModel_c12InitCylinderEv(void *);
'''


def slices(projected, free_c, symbols, data_symbols):
    for module in (9, 62, 69, 85, 89, 94, 98, 100, 102):
        path=ROOT/('external/sm64ds-pc/config/arm9/overlays/ov%03d/symbols.txt'%module)
        for line in path.read_text().splitlines():
            match=re.match(r'(\S+) kind:(\w+).*?addr:0x([0-9a-f]+)',line)
            if match:
                if match[2]=='function': symbols.setdefault(match[1],int(match[3],16))
                else: data_symbols[match[1]]=int(match[3],16)
    symbols.update({'_ZN8daDoor_c13InitResourcesEv':0x021455a0,
                    '_ZN9daSBird_c13InitResourcesEv':0x0211197c,
                    '_ZN9daBtfly_c13InitResourcesEv':0x02141c6c,
                    '_ZN15daObjTatefuda_c18UpdateShadowMatrixEv':0x020baf80,
                    '_ZN11daC_Jugem_c8SetStateEPMS_FivE':0x0212e728})
    rows=[]

    def add(unit,rel,overlay,methods,fields=None,replacements=None,declarations='',extra=''):
        raw=(ROOT/rel).read_text()
        bodies=[]
        for signature,export,parameters in methods:
            body=function(raw,signature)
            result=signature.removeprefix('extern "C" ').split()[0]
            body=replace_once(body,signature,result+' '+export+'('+parameters+')')
            for old,new in (replacements or {}).items(): body=body.replace(old,new)
            body=re.sub(r'\bthis\b','object',body)
            for name,(typ,offset) in {**ACTOR_FIELDS,**(fields or {})}.items():
                body=re.sub(r'(?<![.>])\b'+name+r'\b',
                            '(*('+typ+' *)((char *)object+0x%x))'%offset,body)
            bodies.append(body)
        body_text='\n'.join(bodies)
        # Shared API declarations are a convenience for the reviewed views;
        # emit call veneers only for functions these complete bodies call.
        def used_declaration(match):
            declaration=match.group()
            name=re.search(r'\b(\w+)\s*\(',declaration)
            return declaration if not name or re.search(r'\b'+name[1]+r'\b',body_text) else ''
        declarations=re.sub(r'[^;{}]+;',used_declaration,declarations)
        row,bindings=projected(rel,body_text,declarations,
            [export for _,export,_ in methods],extra=extra,auto_globals=False)
        row['unit']=unit
        for binding in bindings: binding['overlay']=overlay
        rows.append((row,bindings))

    def method(cls,name,result='int',export=None):
        return (result+' '+cls+'::'+name+'()',
                export or '_ZN'+str(len(cls))+cls+str(len(name))+name+'Ev','void *object')

    add('stage_bg_actor_transform','src/runtime/actor/d_bg_actor.cpp',2,
        [method('dBgActor_c','UpdateModelPosAndRotY','void'),
         method('dBgActor_c','UpdateClsnPosAndRot','void')],
        {'mModel':('Model',0xd4),'mClsnMat':('Matrix4x3',0x2ec)},
        {'mMeshCollider.Transform(mClsnMat, mAngleY)':
            '_ZN10dBgW_KcMbg9TransformERK9Matrix4x3s((char *)object+0x124,&mClsnMat,mAngleY)'},'''
void Matrix4x3_FromRotationY(Matrix4x3 *,int);
void _ZN10dBgW_KcMbg9TransformERK9Matrix4x3s(void *,Matrix4x3 *,s16);
''')

    add('stage_metalnet_init','src/game/actors/d_a_obj_mc_metalnet.cpp',9,
        [method('daObjMc_Metalnet_c','InitResources','s32'),
         method('daObjMc_Metalnet_c','OnPendingDestroy','void')],{},
        {'dExtSimpleModel_c::LoadFile(data_ov009_02113e90)':
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov009_02113e90)',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'UpdateModelPosAndRotY()':'_ZN10dBgActor_c21UpdateModelPosAndRotYEv(object)',
         'UpdateClsnPosAndRot()':'_ZN10dBgActor_c19UpdateClsnPosAndRotEv(object)',
         'dBgW_Kc::LoadFile(data_ov009_02113e88)':
            '_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(&data_ov009_02113e88)',
         'KCL_File *':'void *','(KCL_File *)':'','(BMD_File *)':'',
         '''((dBgW_KcMbg *)(&mMeshCollider))
        ->dBgW_KcMbg::SetFile(kcl, *(&mClsnMat), 0x1000, mAngleY,
                              *(CLPS_Block *)(&data_ov009_02112bf8))''':
            '_ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block((char *)object+0x124,kcl,(Matrix4x3 *)((char *)object+0x2ec),0x1000,mAngleY,data_ov009_02112bf8)',
         'IsVsMode()':'(data_0209f2d8 == 1)'},MODEL_API+'''
extern SharedFilePtr data_ov009_02113e90,data_ov009_02113e88;
extern u8 data_ov009_02112bf8[],data_0209f2d8; extern int data_0209caa0[];
void _ZN10dBgActor_c21UpdateModelPosAndRotYEv(void *);
void _ZN10dBgActor_c19UpdateClsnPosAndRotEv(void *);
void *_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block(void *,void *,Matrix4x3 *,int,s16,void *);
u8 NumStars();
''')

    add('stage_signpost_init','src/game/actors/d_a_obj_tatefuda.cpp',2,
        [method('daObjTatefuda_c','InitResources',export='_ZN8SignPost13InitResourcesEv'),
         method('daObjTatefuda_c','UpdateShadowMatrix','void')],
        {'mShadowMat':('Matrix4x3',0x380),
         'mPoundsLeft':('u8',0x58e),
         'mHomePosX':('s32',0x3b0),'mHomePosY':('s32',0x3b4),'mHomePosZ':('s32',0x3b8),
         'mHomeAngleX':('s16',0x3bc),'mHomeAngleY':('s16',0x3be),'mHomeAngleZ':('s16',0x3c0)},
        {'dExtSimpleModel_c::LoadFile(SignPost_ModelFile)':
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&SignPost_ModelFile)',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         '(BMD_File*)':'','(KCL_File *)':'',
         'mShadowModel.InitCuboid()':'_ZN17dExtShadowModel_c10InitCuboidEv((char *)object+0x358)',
         'dBgCh_Gnd rg;':'char rg[0x50]; _ZN9dBgCh_GndC1Ev(rg);',
         'rg.SetObjAndPos(*(Vector3*)&v, (dActor_c*)0)':
            '_ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(rg,&v,0)',
         'rg.DetectClsn()':'_ZN9dBgCh_Gnd10DetectClsnEv(rg)',
         'rg.clsnY':'(*(s32 *)(rg+0x44))',
         'UpdateModelPosAndRotY()':'_ZN10dBgActor_c21UpdateModelPosAndRotYEv(object)',
         'UpdateClsnPosAndRot()':'_ZN10dBgActor_c19UpdateClsnPosAndRotEv(object)',
         'UpdateShadowMatrix()':'_ZN15daObjTatefuda_c18UpdateShadowMatrixEv(object)',
         'dBgW_Kc::LoadFile(SignPost_ClsnFile)':
            '_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(&SignPost_ClsnFile)',
         '''((dBgW_KcMbg *)(&mMeshCollider))
        ->dBgW_KcMbg::SetFile(kf, mClsnMat, 0x199, mAngleY, data_ov002_0210d714)''':
            '_ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block((char *)object+0x124,kf,(Matrix4x3 *)((char *)object+0x2ec),0x199,mAngleY,data_ov002_0210d714)',
         '&mdCcAc_c':'((char *)object+0x320)',
         '&mWithMeshClsn':'((char *)object+0x3c8)',
         '(dActor_c*)((char *)this)':'object',
         'mWithMeshClsn.StartDetectingWater()':
            '_ZN10dBgCh_Actr19StartDetectingWaterEv((char *)object+0x3c8)',
         'return 1;':'_ZN9dBgCh_GndD1Ev(rg); return 1;',
         '.t.x':'.m[9]','.t.y':'.m[10]','.t.z':'.m[11]'},MODEL_API+'''
extern SharedFilePtr SignPost_ModelFile,SignPost_ClsnFile;
extern u8 data_ov002_0210d714[];
int _ZN17dExtShadowModel_c10InitCuboidEv(void *);
void *_ZN9dBgCh_GndC1Ev(void *); void _ZN9dBgCh_GndD1Ev(void *);
void _ZN9dBgCh_Gnd12SetObjAndPosERK7Vector3P8dActor_c(void *,Vec3 *,void *);
int _ZN9dBgCh_Gnd10DetectClsnEv(void *);
void _ZN10dBgActor_c21UpdateModelPosAndRotYEv(void *);
void _ZN10dBgActor_c19UpdateClsnPosAndRotEv(void *);
void _ZN15daObjTatefuda_c18UpdateShadowMatrixEv(void *);
void *_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block(void *,void *,Matrix4x3 *,int,s16,void *);
void _ZN7dCcAc_c4InitEP8dActor_c5Fix12IiES3_jj(void *,void *,int,int,u32,u32);
void _ZN10dBgCh_Actr4InitEP8dActor_c5Fix12IiES3_P10Vector3_16S5_(void *,void *,int,int,void *,void *);
void _ZN10dBgCh_Actr19StartDetectingWaterEv(void *);
void Matrix4x3_FromRotationY(Matrix4x3 *,int);
''','struct Vec3 { s32 x,y,z; };\n')

    add('stage_sign_init','src/game/actors/d_a_obj_kanban.cpp',85,
        [method('daObjKanban_c','InitResources',export='_ZN8WallSign13InitResourcesEv')],{},
        {'dExtSimpleModel_c::LoadFile(data_ov085_02130858)':
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov085_02130858)',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         '(BMD_File *)':'','struct Vector3':'Vec3',
         'UpdateModelPosAndRotY()':'_ZN10dBgActor_c21UpdateModelPosAndRotYEv(object)'},MODEL_API+'''
extern SharedFilePtr data_ov085_02130858; extern Vec3 data_ov085_0212f29c;
void _ZN10dBgActor_c21UpdateModelPosAndRotYEv(void *);
void _ZN10dCcAcPos_c4InitEP8dActor_cRK7Vector35Fix12IiES6_jj(void *,void *,Vec3 *,int,int,u32,u32);
''','struct Vec3 { s32 x,y,z; };\n')

    rel='src/game/actors/d_a_door.cpp'
    extra='struct Vec3i { s32 x,y,z; }; struct M48 { s32 w[12]; };\n'
    extra+=re.search(r'struct DoorEntry \{.*?\n\};',(ROOT/rel).read_text(),re.S).group()+'\n'
    add('stage_door_init',rel,100,[method('daDoor_c','InitResources','s32')],
        {'mModel':('Model',0xd4),'mKeyFile':('void *',0x13c),
         'mKeyModel':('Model *',0x138),'mKeyModelIdx':('s8',0x144)},
        {'__register_global_object(':'func_020731dc(',
         '(void *)_ZN7Vector3D1Ev':'(void *)0x020072c0u',
         'dExtSimpleModel_c::LoadFile(*e->file)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(e->file)',
         'dExtSimpleModel_c::LoadFile(*e->keyFile)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(e->keyFile)',
         'dExtSimpleModel_c::LoadFile(*(SharedFilePtr *)mKeyFile)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr((SharedFilePtr *)mKeyFile)',
         'dExtFrameCtrl_c::LoadFile(data_ov100_02148744)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov100_02148744)',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'mKeyModel->SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii(mKeyModel,',
         'mModel.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0xd4,',
         'mKeyModel = new dExtSimpleModel_c;':
             'void *decoration=_Znwj(0x50);\nif(decoration) decoration=_ZN17dExtSimpleModel_cC1Ev(decoration);\nmKeyModel=(Model *)decoration;',
         '(BCA_File *)':'','func_ov100_02145370()':'func_ov100_02145370(object)',
         'func_ov100_021453d8(':'func_ov100_021453d8(object,',
         'DOOR_FIRST_KEY_MODEL':'9','DOOR_LAST_KEY_MODEL':'13'},
        MODEL_API+'''
extern int data_ov100_02148710; extern Vec3i data_ov100_021487c0;
extern u8 data_ov100_021487b4[]; extern DoorEntry data_ov100_02148204[];
extern u8 data_0209f2d8; extern int data_ov002_0211094c;
extern void *data_ov089_02132894[]; extern int data_ov089_02132c50;
extern SharedFilePtr data_ov100_02148744; extern M48 data_020a0e68;
extern u8 data_0209f250; extern void *data_0209f394[];
extern u8 data_ov100_02148914[],data_ov100_021488b4[];
void func_020731dc(void *,void *,void *);
void Vec3_RotateYAndTranslate(void *,void *,s16,void *);
void LoadKeyModels(int); void Vec3_Asr(Vec3i *,void *,int);
void Matrix4x3_FromTranslation(void *,int,int,int);
void Matrix4x3_ApplyInPlaceToRotationY(void *,s16);
void *_Znwj(u32); void *_ZN17dExtSimpleModel_cC1Ev(void *);
void *func_ov100_02145370(void *); int func_ov100_021453d8(void *,void *,int);
''',extra)

    add('stage_key_models','src/game/actors/d_a_obj_key.cpp',89,
        [('extern "C" void LoadKeyModels(int kind)','LoadKeyModels','int kind')],{},
        {'dExtSimpleModel_c::LoadFile(*(SharedFilePtr *)data_ov089_02132894[kind])':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr((SharedFilePtr *)data_ov089_02132894[kind])',
         'dExtSimpleModel_c::LoadFile(*extra)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(extra)'},
        MODEL_API+'extern void *data_ov089_02132894[],*data_ov089_021328b4[];\n')

    add('stage_scene_trigger_init','src/game/actors/d_a_ch_scene.cpp',2,
        [method('daChScene_c','InitResources','s32')],{'mInvMat':('Matrix4x3',0xd4)}, {},'''
extern s8 data_0209f2f8; extern u8 data_0209f220;
int IsStarCollectedInLevel(int,int);
void Matrix4x3_FromTranslation(Matrix4x3 *,int,int,int);
void Matrix4x3_ApplyInPlaceToRotationY(Matrix4x3 *,s16);
void Matrix4x3_ApplyInPlaceToRotationX(Matrix4x3 *,s16);
void InvMat4x3(Matrix4x3 *,Matrix4x3 *);
''')

    add('stage_castle_flag_init','src/game/actors/d_a_mc_flag.cpp',9,
        [method('daMcFlag_c','InitResources','s32')],{},
        {'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'mModelAnim.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0xd4,',
         'dExtSimpleModel_c::LoadFile(data_ov009_02113eb8)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov009_02113eb8)',
         'dExtFrameCtrl_c::LoadFile(data_ov009_02113eb0)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov009_02113eb0)',
         '(BCA_File *)':''},
        MODEL_API+'extern SharedFilePtr data_ov009_02113eb8,data_ov009_02113eb0;\n')

    rel='src/game/actors/d_a_tree.cpp'
    extra='''
struct Vector3 { s32 x,y,z; }; struct TreeClsn { u8 bytes[0x3c]; };
struct TreeNode { Vector3 pos; TreeClsn clsn; TreeNode *next; };
static_assert(sizeof(TreeNode)==0x4c && offsetof(TreeNode,next)==0x48);
'''+re.search(r'enum \{.*?\n\};',(ROOT/rel).read_text(),re.S).group()+'\n'
    add('stage_tree_init',rel,2,[method('daTree_c','InitResources'),
        method('daTree_c','OnPendingDestroy','void')],{},
        {'mModel[variant].LoadAndSetFile(':
             '_ZN17dExtSimpleModel_c14LoadAndSetFileEtii((char *)object+0xd4+variant*0x50,',
         '::operator new(':'_Znwj(', ', mPos,':', (Vector3 *)((char *)object+0x5c),'},'''
extern TreeNode *data_ov002_02110a48[5]; extern u16 data_ov002_0210abb8[];
void *_Znwj(u32); void *_ZN8dCcPos_cC1Ev(void *);
int _ZN17dExtSimpleModel_c14LoadAndSetFileEtii(void *,u16,int,int);
int *Vec3_AsrInPlace(Vector3 *,int);
void _ZN8dCcPos_c4InitERK7Vector35Fix12IiES4_jj(void *,Vector3 *,int,int,u32,u32);
''',extra)

    add('stage_bird_init','src/game/actors/d_a_s_bird.cpp',9,
        [method('daSBird_c','InitResources','s32')],
        {'mIsLeader':('u8',0x180),'mOwnerID':('u32',0x178),
         'mTargetPos':('Vec3i',0x160),'mState':('s32',0x17c)},
        {'dExtSimpleModel_c::LoadFile(data_ov009_02113c20)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov009_02113c20)',
         'dExtFrameCtrl_c::LoadFile(data_ov009_02113c28)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov009_02113c28)',
         'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'mModelAnim.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0xd4,',
         'mShadowModel.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x138)',
         'BCA_File *':'void *','(BCA_File *)':''},
        MODEL_API+'extern SharedFilePtr data_ov009_02113c20,data_ov009_02113c28;\n',
        'struct Vec3i { s32 x,y,z; };\n')

    add('stage_ambient_sound','src/game/actors/d_a_set_se.cpp',2,
        [method('daSetSE_c',name,'void' if name=='OnPendingDestroy' else 'int')
         for name in ('InitResources','Render','OnPendingDestroy','CleanupResources')],{}, {},'''
extern s8 data_0209f2f8; extern u8 data_0209f220,data_0209f2d8;
extern int data_0209caa0[],data_0209fc48,data_ov002_02110aec;
int IsStarCollectedInLevel(s8,int);
''')

    add('stage_butterfly_init','src/game/actors/d_a_btfly.cpp',100,
        [method('daBtfly_c','InitResources')],
        {'mHomePosX':('s32',0x3d4),'mHomePosY':('s32',0x3d8),'mHomePosZ':('s32',0x3dc),
         'mStateTimer':('s32',0x3e8),'mState':('s32',0x3e4),'mScale':('s32',0x3e0),
         'mKind':('u8',0x3f0),'mUseAnimModel':('u8',0x3f1)},
        {'gButterflyFlyAnimationFile':'data_ov100_02148600',
         'gButterflyModelFile':'data_ov100_02148608',
         **{'dExtSimpleModel_c::LoadFile('+name+')':
              '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&'+name+')'
              for name in ('data_ov002_0210d9d8','data_ov100_02148668','data_ov100_02148608')},
         'dExtFrameCtrl_c::LoadFile(data_ov100_02148600)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov100_02148600)',
         'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0x138,',
         'mModelAnim.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0xd4,',
         'mShadowModel1.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x188)',
         'mShadowModel2.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x1b0)',
         'mModelAnim.GetFrameCount()':'_ZNK15dExtFrameCtrl_c13GetFrameCountEv((char *)object+0x124)',
         'mModelAnim.currFrame':'(*(s32 *)((char *)object+0x12c))',
         '&mWithMeshClsn':'((char *)object+0x1d8)','&mdCcAcPos_c':'((char *)object+0x394)',
         '(BCA_File *)':''},MODEL_API+'''
extern SharedFilePtr data_ov002_0210d9d8,data_ov100_02148600,data_ov100_02148608,data_ov100_02148668;
extern int data_0209e650;
void _ZN10dBgCh_Actr4InitEP8dActor_c5Fix12IiES3_P10Vector3_16S5_(void *,void *,int,int,void *,void *);
void _ZN10dCcAcPos_c4InitEP8dActor_cRK7Vector35Fix12IiES6_jj(void *,void *,void *,int,int,u32,u32);
int RandomIntInternal(int *); int _ZNK15dExtFrameCtrl_c13GetFrameCountEv(void *);
''','struct Vec3 { s32 x,y,z; };\n')

    rel='src/game/actors/d_a_cnn.cpp'
    extra=re.search(r'struct CnnBone \{.*?\n\};',(ROOT/rel).read_text(),re.S).group()+'\n'
    add('stage_cannon_init',rel,98,
        [method('daCnn_c','InitResources',export='_ZN6Cannon13InitResourcesEv'),
         method('daCnn_c','func_ov098_0213b15c','void',export='func_ov098_0213b15c')],
        {'mModel':('Model',0xd4),'mSpawnPosX':('s32',0x15c),'mSpawnPosY':('s32',0x160),
         'mSpawnPosZ':('s32',0x164),'mTimer':('s32',0x174),'mTargetPitch':('s16',0x178),
         'mTargetAngleY':('s16',0x17a),'mHomeAngleY':('s16',0x17c),
         'mState':('s32',0x180),'mVariant':('u8',0x184),'mLoadStep':('u8',0x185),
         'mBarrelRestY':('s32',0x194)},
        {'dExtSimpleModel_c::LoadFile(':'_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'func_ov098_0213b15c()':'func_ov098_0213b15c(object)',
         '&mdCcAc_c':'((char *)object+0x124)'},MODEL_API+'''
extern SharedFilePtr data_ov098_0213c8e8,data_ov098_0213c91c,data_ov002_0210da38;
extern int data_0209e650;
void _ZN7dCcAc_c4InitEP8dActor_c5Fix12IiES3_jj(void *,void *,int,int,u32,u32);
void func_ov098_0213b15c(void *); int RandomIntInternal(int *); int AngleDiff(int,int);
''',extra)

    add('stage_fish_init','src/game/actors/d_a_fish.cpp',100,
        [method('daFish_c','InitResources','s32',export='_ZN4Fish13InitResourcesEv')],
        {'mModelIndex':('u8',0x15b),'mVariant':('u8',0x15c),'mIsSpawner':('u8',0x159),
         'mSpawnerID':('u32',0x13c),'mState':('s32',0x14c),
         'mSurfaceY':('s32',0x140),'mStateTimer':('s32',0x150)},
        {'dExtFrameCtrl_c::LoadFile(data_ov100_021489cc)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov100_021489cc)',
         'dExtSimpleModel_c::LoadFile(*data_ov100_021473a4[mModelIndex])':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(data_ov100_021473a4[mModelIndex])',
         'dExtFrameCtrl_c::LoadFile(*data_ov100_021473b0[mModelIndex])':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(data_ov100_021473b0[mModelIndex])',
         'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
         'mModelAnim.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0xd4,',
         '(BCA_File *)':''},MODEL_API+'''
extern SharedFilePtr data_ov100_021489cc;
extern SharedFilePtr *data_ov100_021473a4[],*data_ov100_021473b0[];
''')

    add('stage_mushroom_init','src/game/actors/d_a_1up.cpp',2,
        [method('da1up_c','InitResources'),method('da1up_c','OnPendingDestroy','void')],
        {'actorID':('u16',12),'mMushroomType':('s32',0x384),
         'mState':('s32',0x388),'mShown':('u8',0x38e),'mBlinkOn':('u8',0x38f),
         'mUnlockCount':('s32',0x390),'mParticleID':('u32',0x394),
         'mSpawnPosX':('s32',0x378),'mSpawnPosY':('s32',0x37c),'mSpawnPosZ':('s32',0x380)},
        {'BMD_File* f;':'void *f;', '(BMD_File *)':'',
         'dExtSimpleModel_c::LoadFile(data_ov002_0210d9d8)':
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov002_0210d9d8)',
         'dExtSimpleModel_c::LoadFile(data_ov002_0210da30)':
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov002_0210da30)',
         'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0x300,',
         'mShadowModel.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x350)',
         '&mdCcAc_c':'((char *)object+0x110)',
         '(dActor_c*)this':'object',
         'mdCcAc_c.vulnFlags':'(*(u32 *)((char *)object+0x12c))',
         'mdCcAc_c.flags':'(*(u32 *)((char *)object+0x128))',
         '&mWithMeshClsn':'((char *)object+0x144)',
         'mWithMeshClsn.SetLimMovFlag()':'_ZN10dBgCh_Actr13SetLimMovFlagEv((char *)object+0x144)',
         'MarkForDestruction()':'_ZN7fBase_c18MarkForDestructionEv(object)'},MODEL_API+'''
int IsStarCollectedInCurLevel(u32);
void _ZN7fBase_c18MarkForDestructionEv(void *);
void _ZN7dCcAc_c4InitEP8dActor_c5Fix12IiES3_jj(void *,void *,int,int,u32,u32);
void _ZN10dBgCh_Actr4InitEP8dActor_c5Fix12IiES3_P10Vector3_16S5_(void *,void *,int,int,void *,void *);
void _ZN10dBgCh_Actr13SetLimMovFlagEv(void *);
''','enum { ACTOR_ONEUPKINOKO=276,ACTOR_SCALEUP_KINOKO=277,CC_VULN_YOSHI_TONGUE=0x8000,CC_FLAGS_DISABLED=1,ACTOR_FLAG_CLIP_TEST=1 };\n')

    # daMip_c::InitResources (ov085 0x0212c7fc). Its in-body file-scope
    # externs move to the declaration block; the body itself is unchanged.
    mip_raw=function((ROOT/'src/game/actors/d_a_mip.cpp').read_text(),'int daMip_c::InitResources()')
    mip_raw+=function((ROOT/'src/game/actors/d_a_mip.cpp').read_text(),'int daMip_c::CleanupResources()')
    mip_replacements={line:'' for line in re.findall(r'\n\s*extern [^;\n]+;',mip_raw)}
    mip_replacements.update({
         'gMipRunAnimationFile':'data_ov085_021305d0',
         '(BMD_File *)':'',
         'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0x300,',
         'mModelAnim.speed':'(*(s32 *)((char *)object+0x35c))',
         '*(s32*)((char*)mModelAnim.data.materials + 0x20)':
             '*(s32 *)((char *)(*(void **)((char *)object+0x30c)) + 0x20)',
         'mShadowModel1.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x368)',
         'mShadowModel2.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x3c0)',
         '&mdCcAc_c':'((char *)object+0x110)',
         '&mWithMeshClsn':'((char *)object+0x144)',
         'PathPtr path;':'struct MipPath path; path.def=0; path.unk_004=0;',
         'path.FromID(mPathId)':'_ZN7PathPtr6FromIDEj(&path,mPathId)',
         'path.GetNode(mPos, mPathNodeIndex)':
             '_ZNK7PathPtr7GetNodeER7Vector3j(&path,(char *)object+0x5c,mPathNodeIndex)',
         'path.NumNodes()':'_ZNK7PathPtr8NumNodesEv(&path)',
         'UpdateMatrixAndShadow()':'_ZN7daMip_c21UpdateMatrixAndShadowEv(object)',
         'ClosestPlayer()':'_ZN8dActor_c13ClosestPlayerEv(object)',
         '((Player *)player)->mCharacter':'(*(u8 *)((char *)player+0x6d9))',
         '((Player *)closest)->param1':'(*(u32 *)((char *)closest+0x8))',
         'SetState(&data_ov085_021306cc)':'_ZN7daMip_c8SetStateEPv(object,&data_ov085_021306cc)'})
    for name in ('data_ov085_021305b8','data_ov085_021305d0','data_ov085_021305b0',
                 'data_ov085_021305c8','data_ov085_021305c0','data_ov085_021305d8'):
        mip_replacements['dExtFrameCtrl_c::LoadFile(*(SharedFilePtr *)&'+name+')']=\
            '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&'+name+')'
        mip_replacements['dExtSimpleModel_c::LoadFile(*(SharedFilePtr *)&'+name+')']=\
            '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&'+name+')'
    for name in ('data_ov085_021305d8','data_ov085_021305b8','data_ov085_021305d0','data_ov085_021305b0',
                 'data_ov085_021305c8','data_ov085_021305c0','data_ov085_021305e0'):
        mip_replacements['((SharedFilePtr *)(&'+name+'))->Release()']=\
            '_ZN13SharedFilePtr7ReleaseEv(&'+name+')'
    mip_replacements['dExtSimpleModel_c::LoadFile(*(SharedFilePtr *)&data_ov085_021305e0)']=\
        '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov085_021305e0)'
    add('stage_mip_init','src/game/actors/d_a_mip.cpp',85,
        [method('daMip_c','InitResources'),method('daMip_c','CleanupResources'),
         method('daMip_c','OnPendingDestroy','void')],
        {'mEatingPlayer':('s32',0xd0),'mColorVariant':('s32',0x420),
         'mIsDisabled':('u8',0x428),'mIsGlowing':('u8',0x429),
         'mPathId':('s32',0x438),'mRabbitId':('s32',0x43c),'mCharacterId':('s32',0x440),
         'mNumPathNodes':('s32',0x444),'mPathNodeIndex':('s32',0x448),
         'mTalkingPlayer':('u32',0x45c),'mMaterialColor':('s32',0x468)},
        mip_replacements,MODEL_API+'''
extern SharedFilePtr data_ov085_021305b8,data_ov085_021305d0,data_ov085_021305b0;
extern SharedFilePtr data_ov085_021305c8,data_ov085_021305c0,data_ov085_021305d8,data_ov085_021305e0;
extern struct MipStatePair data_ov085_021306cc;
extern s32 data_ov085_021305ac;
extern int data_0209caa0[];
extern s8 data_0209f2f8;
extern int data_0209e650;
void _ZN7dCcAc_c4InitEP8dActor_c5Fix12IiES3_jj(void *,void *,int,int,u32,u32);
void _ZN10dBgCh_Actr4InitEP8dActor_c5Fix12IiES3_P10Vector3_16S5_(void *,void *,int,int,void *,void *);
void _ZN7PathPtr6FromIDEj(struct MipPath *,u32);
void _ZNK7PathPtr7GetNodeER7Vector3j(struct MipPath *,void *,u32);
u32 _ZNK7PathPtr8NumNodesEv(struct MipPath *);
void _ZN7daMip_c21UpdateMatrixAndShadowEv(void *);
void *_ZN8dActor_c13ClosestPlayerEv(void *);
int func_02013890(int,int);
u32 RandomIntInternal(int *);
u8 NumStars(void);
void _ZN7daMip_c8SetStateEPv(void *,void *);
void _ZN13SharedFilePtr7ReleaseEv(SharedFilePtr *);
''','struct MipPath { void *def; s32 unk_004; };\nstruct MipStatePair { u32 w[4]; };\n')

    # daC_Jugem_c (ov085 cartridge symbols spell it LakituBro).
    jugem_files={'data_ov085_0213074c':'BMD','data_ov085_02130744':'BCA','data_ov085_0213073c':'BTP'}
    jugem_replacements={
         'BMD_File *bmd = (BMD_File *)':'void *bmd = ',
         'dExtSimpleModel_c::LoadFile(data_ov085_0213074c)':
             '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov085_0213074c)',
         'dExtFrameCtrl_c::LoadFile(data_ov085_02130744)':
             '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov085_02130744)',
         'dExtAnmTexPat_c::LoadFile(data_ov085_0213073c)':
             '_ZN15dExtAnmTexPat_c8LoadFileER13SharedFilePtr(&data_ov085_0213073c)',
         'mModelAnim1.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0x110,',
         'mModelAnim1.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0x110,',
         'mModelAnim1.speed':'(*(s32 *)((char *)object+0x16c))',
         'mShadowModel1.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x1f0)',
         'mShadowModel2.InitCylinder()':'_ZN17dExtShadowModel_c12InitCylinderEv((char *)object+0x218)',
         'dExtAnmTexPat_c::Prepare(**(BMD_File **)((char *)&data_ov085_0213074c + 4),\n                             **(BTP_File **)((char *)&data_ov085_0213073c + 4))':
             '_ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(data_ov085_0213074c.filePtr,data_ov085_0213073c.filePtr)',
         '*(BCA_File **)((char *)&data_ov085_02130744 + 4)':'data_ov085_02130744.filePtr',
         'mTextureSequence.SetFile(**(BTP_File **)((char *)&data_ov085_0213073c + 4),':
             '_ZN15TextureSequence7SetFileER8BTP_Filei5Fix12IiEj((char *)object+0x1d8,data_ov085_0213073c.filePtr,',
         'SetState(':'_ZN11daC_Jugem_c8SetStateEPMS_FivE(object,'}
    for name in jugem_files:
        jugem_replacements[name+'.Release()']='_ZN13SharedFilePtr7ReleaseEv(&'+name+')'
    add('stage_jugem_init','src/game/actors/d_a_c_jugem.cpp',85,
        [method('daC_Jugem_c','InitResources',export='_ZN9LakituBro13InitResourcesEv'),
         method('daC_Jugem_c','CleanupResources',export='_ZN9LakituBro16CleanupResourcesEv'),
         method('daC_Jugem_c','OnPendingDestroy','void',export='_ZN9LakituBro16OnPendingDestroyEv')],
        {'mVariant':('s32',0x2d0),'mHidden':('u8',0x2dc)},
        jugem_replacements,MODEL_API+'''
extern SharedFilePtr data_ov085_0213074c,data_ov085_02130744,data_ov085_0213073c;
extern u32 data_ov085_02130790[4],data_ov085_021307d0[4],data_ov085_021307e0[4];
extern int data_0209caa0[];
void *_ZN15dExtAnmTexPat_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN15TextureSequence7PrepareER8BMD_FileR8BTP_File(void *,void *);
void _ZN15TextureSequence7SetFileER8BTP_Filei5Fix12IiEj(void *,void *,int,int,u32);
int _ZN11daC_Jugem_c8SetStateEPMS_FivE(void *,void *);
void _ZN13SharedFilePtr7ReleaseEv(SharedFilePtr *);
''')

    # daC_Jugem_c::StateArriveInit, from the recovered isolated unit.
    add('stage_jugem_arrive_init','external/sm64ds-pc/src/func_ov085_0212df84.cpp',85,
        [('extern "C" int func_ov085_0212df84(char* c)','func_ov085_0212df84','char *c')],{},{},'''
void *_ZN8dActor_c13ClosestPlayerEv(void *);
void _ZN6Camera9SetFlag_3Ev(void *);
void _ZN6Camera9SetLookAtERK7Vector3(void *,const struct Vector3 *);
void _ZN6Camera6SetPosERK7Vector3(void *,const struct Vector3 *);
void func_ov002_020d228c(void *);
extern void *data_0209f318;
''','struct Vector3 { s32 x,y,z; };\n')

    for name in ('func_ov100_0214491c','func_ov100_02144c64'):
        row,bindings=free_c('external/sm64ds-pc/src/'+name+'.c')
        for binding in bindings: binding['overlay']=100
        rows.append((row,bindings))
    return rows

"""Reviewed complete resource and transform bodies for original item blocks."""
import re
from pathlib import Path
from native_sources import function, replace_once

ROOT=Path(__file__).resolve().parents[2]


def slices(projected,symbols):
    rel='src/game/actors/d_a_obj_hatena_block.cpp'
    raw=(ROOT/rel).read_text()
    specs=[('int daObjHatenaBlock_c::InitResources()',
            '_ZN13QuestionBlock13InitResourcesEv'),
           ('void daObjHatenaBlock_c::func_ov102_02149ff0()',
            'func_ov102_02149ff0'),
           ('void daObjHatenaBlock_c::func_ov102_02149e38()',
            'func_ov102_02149e38'),
           ('void daObjHatenaBlock_c::func_ov102_02149d80()',
            'func_ov102_02149d80')]
    fields={
        'actorID':('u16',12),'param1':('u32',8),
        'mModel':('Model',0xd4),'mModelAnim':('Model',0x320),
        'mShadowMat':('Matrix4x3',0x3ac),'mClsnMat':('Matrix4x3',0x2ec),
        'mPosX':('s32',0x5c),'mPosY':('s32',0x60),'mPosZ':('s32',0x64),
        'mAngleY':('s16',0x8e),'mTerminalVelocity':('s32',0xa0),
        'mScaleX':('s32',0x80),'mScaleY':('s32',0x84),'mScaleZ':('s32',0x88),
        'mVertAccel':('s32',0x9c),'mBounceYOffs':('s32',0x3dc),
        'mHomePosY':('s32',0x3e0),'mContentType':('u8',0x3f3),
        'mStarId':('u8',0x3f1),'mStarTracked':('u8',0x3f0),
    }
    bodies=[]
    for signature,export in specs:
        body=replace_once(function(raw,signature),signature,
                          signature.split()[0]+' '+export+'(void *object)')
        body=re.sub(r'dExtSimpleModel_c::LoadFile\(\*\(SharedFilePtr \*\)(\w+)\)',
            r'_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr((SharedFilePtr *)\1)',body)
        body=re.sub(r'dExtSimpleModel_c::LoadFile\((\w+)\)',
            r'_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&\1)',body)
        for old,new in {
            '(BMD_File *)':'','(BCA_File *)':'',
            'mModelAnim.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0x320,',
            'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
            'mModelAnim.SetAnim(':'_ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit((char *)object+0x320,',
            'dExtFrameCtrl_c::LoadFile(data_ov102_0214e7f8)':
                '_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(&data_ov102_0214e7f8)',
            'mShadowModel.InitCuboid()':'_ZN17dExtShadowModel_c10InitCuboidEv((char *)object+0x384)',
            'func_ov102_02149da8(STATE_IDLE)':'func_ov102_02149da8(object,STATE_IDLE)',
            'func_ov102_02149ff0()':'func_ov102_02149ff0(object)',
            'func_ov102_02149e38()':'func_ov102_02149e38(object)',
            'dBgW_Kc::LoadFile(data_ov102_0214e7d0)':
                '_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(&data_ov102_0214e7d0)',
            '''((dBgW_KcMbg *)(&mMeshCollider))
            ->dBgW_KcMbg::SetFile((KCL_File *)(kcl), *(const Matrix4x3 *)(&mClsnMat), 0x199,
                                  mAngleY, *(CLPS_Block *)(&data_ov002_0210d954))''':
                '_ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block((char *)object+0x124,kcl,&mClsnMat,0x199,mAngleY,data_ov002_0210d954)',
            'TrackStar(mStarId, 2)':'_ZN8dActor_c9TrackStarEjj(object,mStarId,2)',
            'mMeshCollider.Transform(mClsnMat, mAngleY)':
                '_ZN10dBgW_KcMbg9TransformERK9Matrix4x3s((char *)object+0x124,&mClsnMat,mAngleY)',
        }.items(): body=body.replace(old,new)
        for name,(typ,offset) in fields.items():
            body=re.sub(r'(?<![.>])\b'+name+r'\b','(*('+typ+' *)((char *)object+0x%x))'%offset,body)
        bodies.append(body)
    actor_constants=re.search(r'enum \{\s*ACTOR_HATENA_BLOCK.*?\n\};',raw,re.S).group()
    header=(ROOT/'include/daObjHatenaBlock_c.h').read_text()
    content=re.search(r'enum Content \{.*?\n    \};',header,re.S).group()
    extra=actor_constants+'\n'+content+'\nenum { STATE_IDLE=0, SAVE_FLAGS1_BIT31=0x80000000 };\n'
    declarations='''
extern int data_0209caa0[];
extern SharedFilePtr data_ov102_0214e7e8,data_ov102_0214e808,data_ov102_0214e7f8;
extern SharedFilePtr data_ov102_0214e800,data_ov102_0214e7f0,data_ov102_0214e7d8,data_ov102_0214e7e0,data_ov102_0214e7d0;
extern SharedFilePtr data_ov002_0210d9e0,data_ov002_0210da40,data_ov002_0210d9a0,data_ov002_0210d9c0;
extern SharedFilePtr data_ov002_0210da18,data_ov002_0210d9d8,data_ov002_0210da30;
extern u8 data_ov002_0210d954[],data_ov002_0210da58[],gPFlowerOpenModelFile[],gPFlowerCloseModelFile[];
void *_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
void *_ZN15dExtFrameCtrl_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
int _ZN11dExtModel_c7SetFileEP8BMD_Fileii(void *,void *,int,int);
void _ZN14dExtAnmModel_c7SetAnimEP8BCA_Fileiit(void *,void *,int,int,u16);
int _ZN17dExtShadowModel_c10InitCuboidEv(void *);
void *_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block(void *,void *,Matrix4x3 *,int,s16,void *);
void _ZN10dBgW_KcMbg9TransformERK9Matrix4x3s(void *,Matrix4x3 *,s16);
int _ZN8dActor_c9TrackStarEjj(void *,u32,u32);
void func_ov102_02149da8(void *,int);
void func_ov102_02149ff0(void *); void func_ov102_02149e38(void *);
void Matrix4x3_FromRotationY(Matrix4x3 *,int);
'''
    row,bindings=projected(rel,'\n'.join(bodies),declarations,
        [spec[1] for spec in specs],extra=extra,auto_globals=False)
    row['unit']='stage_hatena_init'
    for binding in bindings: binding['overlay']=102
    yield row,bindings

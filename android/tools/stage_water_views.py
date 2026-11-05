"""Complete castle water and texture animation over checked cartridge records."""
import re
from pathlib import Path
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]


def slices(projected):
    rel = 'src/game/actors/d_a_obj_mc_water.cpp'
    raw = (ROOT/rel).read_text()
    specs = [
        ('int daObjMcWater_c::InitResources()', '_ZN14daObjMcWater_c13InitResourcesEv'),
        ('int daObjMcWater_c::Behavior()', '_ZN14daObjMcWater_c8BehaviorEv'),
        ('int daObjMcWater_c::Render()', '_ZN14daObjMcWater_c6RenderEv'),
        ('int daObjMcWater_c::CleanupResources()', '_ZN14daObjMcWater_c16CleanupResourcesEv'),
        ('extern "C" void func_ov009_02111b1c(daObjMcWater_c *self)', 'func_ov009_02111b1c'),
    ]
    bodies = []
    for signature, export in specs:
        result = signature.removeprefix('extern "C" ').split()[0]
        body = replace_once(function(raw,signature),signature,result+' '+export+'(void *object)')
        replacements = {
            'dExtSimpleModel_c::LoadFile(data_ov009_02113c68)':
                '_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(&data_ov009_02113c68)',
            'BMD_File *bmd = (BMD_File *)':'void *bmd = ',
            'mModel.SetFile(':'_ZN11dExtModel_c7SetFileEP8BMD_Fileii((char *)object+0xd4,',
            'dExtAnmTexSRT_c::Prepare(*(BMD_File *)data_ov009_02113c68.filePtr, data_ov009_02112bc4)':
                '_ZN15dExtAnmTexSRT_c7PrepareER8BMD_FileR8BTA_File(data_ov009_02113c68.filePtr,data_ov009_02112bc4)',
            'mTexTransformer.SetFile(data_ov009_02112bc4,':
                '_ZN15dExtAnmTexSRT_c7SetFileER8BTA_Fileiit((char *)object+0x320,data_ov009_02112bc4,',
            'UpdateModelPosAndRotY()':'_ZN10dBgActor_c21UpdateModelPosAndRotYEv(object)',
            'UpdateClsnPosAndRot()':'_ZN10dBgActor_c19UpdateClsnPosAndRotEv(object)',
            'KCL_File *kcl = (KCL_File *)dBgW_Kc::LoadFile(data_ov009_02113c70)':
                'void *kcl = _ZN7dBgW_Kc8LoadFileER13SharedFilePtr(&data_ov009_02113c70)',
            '''((dBgW_KcMbg *)(&mMeshCollider))
            ->dBgW_KcMbg::SetFile((KCL_File *)(kcl), *(const Matrix4x3 *)(&mClsnMat), 0x1000,
                                  mAngleY, *(CLPS_Block *)(&data_ov009_02112c38))''':
                '_ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block((char *)object+0x124,kcl,(Matrix4x3 *)((char *)object+0x2ec),0x1000,mAngleY,data_ov009_02112c38)',
            'mMeshCollider.Enable(this)':'_ZN4dBgW6EnableEP8dActor_c((char *)object+0x124,object)',
            'func_ov009_02111b1c(this)':'func_ov009_02111b1c(object)',
            'mTexTransformer.speed':'(*(s32 *)((char *)object+0x32c))',
            'mTexTransformer.Advance()':'_ZN9Animation7AdvanceEv((char *)object+0x320)',
            'mTexTransformer.Update(mModel.data)':
                '_ZN15dExtAnmTexSRT_c6UpdateER15ModelComponents((char *)object+0x320,(char *)object+0xdc)',
            'mModel.Render(0)':'_ZN17dExtSimpleModel_c6RenderEPK7Vector3((char *)object+0xd4,0)',
            'data_ov009_02113c68.Release()':'_ZN13SharedFilePtr7ReleaseEv(&data_ov009_02113c68)',
            'data_ov009_02113c70.Release()':'_ZN13SharedFilePtr7ReleaseEv(&data_ov009_02113c70)',
            'mMeshCollider.IsEnabled()':'_ZN4dBgW9IsEnabledEv((char *)object+0x124)',
            'mMeshCollider.Disable()':'_ZN4dBgW7DisableEv((char *)object+0x124)',
            'self->mPosX':'(*(s32 *)((char *)object+0x5c))',
            'self->mAreaId':'(*(s8 *)((char *)object+0xcc))',
            'dActor_c::Spawn(0xc5, 0, vec[i], 0,':
                '_ZN8dActor_c5SpawnEjjRK7Vector3PK10Vector3_16as(0xc5,0,&vec[i],0,',
        }
        for old,new in replacements.items(): body=body.replace(old,new)
        for field,typ,offset in [('mPosY','s32',0x60),('mAngleY','s16',0x8e)]:
            body=re.sub(r'\b'+field+r'\b','(*('+typ+' *)((char *)object+0x%x))'%offset,body)
        bodies.append(body)
    declarations = '''
extern u8 data_0209f2d8[]; extern int data_0209caa0[],data_0209f32c;
extern SharedFilePtr data_ov009_02113c68,data_ov009_02113c70;
extern u8 data_ov009_02112bc4[],data_ov009_02112c38[];
extern Vector3 data_ov009_02113de0[],data_ov009_02113e34[],data_ov009_02113d8c[];
void *_ZN17dExtSimpleModel_c8LoadFileER13SharedFilePtr(SharedFilePtr *);
int _ZN11dExtModel_c7SetFileEP8BMD_Fileii(void *,void *,int,int);
void _ZN15dExtAnmTexSRT_c7PrepareER8BMD_FileR8BTA_File(void *,void *);
void _ZN15dExtAnmTexSRT_c7SetFileER8BTA_Fileiit(void *,void *,int,int,u16);
void _ZN15dExtAnmTexSRT_c6UpdateER15ModelComponents(void *,void *);
void _ZN17dExtSimpleModel_c6RenderEPK7Vector3(void *,void *);
void _ZN13SharedFilePtr7ReleaseEv(SharedFilePtr *);
int _ZN4dBgW9IsEnabledEv(void *); int _ZN4dBgW7DisableEv(void *);
void _ZN10dBgActor_c21UpdateModelPosAndRotYEv(void *);
void _ZN10dBgActor_c19UpdateClsnPosAndRotEv(void *);
void *_ZN7dBgW_Kc8LoadFileER13SharedFilePtr(SharedFilePtr *);
void _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block(void *,void *,Matrix4x3 *,int,s16,void *);
void _ZN4dBgW6EnableEP8dActor_c(void *,void *);
void func_ov009_02111b1c(void *); void _ZN9Animation7AdvanceEv(void *);
void *_ZN8dActor_c5SpawnEjjRK7Vector3PK10Vector3_16as(u32,u32,Vector3 *,void *,s8,s16);
'''
    row,bindings=projected(rel,'\n'.join(bodies),declarations,
        [spec[1] for spec in specs],extra='struct Vector3 { s32 x,y,z; };\n',auto_globals=False)
    row['unit']='stage_castle_water'
    for binding in bindings: binding['overlay']=9
    yield row,bindings

    rel='src/engine/model/d_ext_simple_model.cpp'
    signature='void dExtSimpleModel_c::Render(const Vector3 *scale)'
    body=replace_once(function((ROOT/rel).read_text(),signature),signature,
        'void _ZN17dExtSimpleModel_c6RenderEPK7Vector3(void *object,void *scale)')
    body=body.replace('&mat4x3','(Matrix4x3 *)((char *)object+0x1c)')
    body=body.replace('data.Render(&temp, (Vector3 *)scale)',
        '_ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3((char *)object+8,&temp,scale)')
    row,bindings=projected(rel,body,'''
extern Matrix4x3 data_0209b3ec;
void MulMat4x3Mat4x3(Matrix4x3 *,Matrix4x3 *,Matrix4x3 *);
void _ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3(void *,Matrix4x3 *,void *);
''',['_ZN17dExtSimpleModel_c6RenderEPK7Vector3'],auto_globals=False)
    row['unit']='stage_simple_model_render'
    yield row,bindings

    rel='src/game/actors/d_a_obj_waterfall.cpp'
    raw=(ROOT/rel).read_text()
    body=replace_once(function(raw,'s32 daObjWaterfall_c::InitResources()'),
        's32 daObjWaterfall_c::InitResources()',
        's32 _ZN16daObjWaterfall_c13InitResourcesEv(void *object)')
    body=body.replace('mParticleID','(*(u16 *)((char *)object+0xd4))')
    row,bindings=projected(rel,body,'extern s8 data_0209f2f8;',
        ['_ZN16daObjWaterfall_c13InitResourcesEv'],
        extra=re.search(r'enum \{.*?\n\};',raw,re.S).group(),auto_globals=False)
    row['unit']='stage_waterfall_init'
    for binding in bindings: binding['overlay']=2
    yield row,bindings

    rel='src/engine/model/d_ext_anm_tex_srt.cpp'
    raw=(ROOT/rel).read_text()
    specs=[
        ('void dExtAnmTexSRT_c::SetFile(BTA_File &animFile, int flags, Fix12i speed, u16 startFrame)',
         '_ZN15dExtAnmTexSRT_c7SetFileER8BTA_Fileiit','void *object,BTA_File *animation,int flags,s32 rate,u16 startFrame'),
        ('void dExtAnmTexSRT_c::Update(ModelComponents &model)',
         '_ZN15dExtAnmTexSRT_c6UpdateER15ModelComponents','void *object,void *model'),
        ('void dExtAnmTexSRT_c::Prepare(BMD_File &model, BTA_File &animFile)',
         '_ZN15dExtAnmTexSRT_c7PrepareER8BMD_FileR8BTA_File','void *model,BTA_File *animation'),
    ]
    bodies=[]
    for signature,export,params in specs:
        body=replace_once(function(raw,signature),signature,'void '+export+'('+params+')')
        for old,new in {
            '&animFile':'animation','animFile.numFrames':'animation->numFrames',
            'this->speed = speed':'(*(s32 *)((char *)object+12)) = rate',
            'SetFlags(flags)':'_ZN9Animation8SetFlagsEi(object,flags)',
            'SetAnimation(animation->numFrames, flags, speed, startFrame)':
                '_ZN9Animation12SetAnimationEti5Fix12IiEt(object,animation->numFrames,flags,rate,startFrame)',
            'func_020469e8(&model,':'func_020469e8(model,',
            'func_02046b64(&model,':'func_02046b64(model,',
        }.items(): body=body.replace(old,new)
        body=re.sub(r'\bfile\b','(*(BTA_File **)((char *)object+16))',body)
        body=re.sub(r'\bcurrFrame\b','(*(s32 *)((char *)object+8))',body)
        bodies.append(body)
    row,bindings=projected(rel,'\n'.join(bodies),'''
void _ZN9Animation8SetFlagsEi(void *,int);
void _ZN9Animation12SetAnimationEti5Fix12IiEt(void *,u16,int,int,u16);
void func_020469e8(void *,BTA_File *,int); void func_02046b64(void *,BTA_File *);
''',[spec[1] for spec in specs],extra='struct BTA_File { u16 numFrames; };\n',auto_globals=False)
    row['unit']='stage_texture_transformer'
    yield row,bindings

    rel='src/engine/animation/d_ext_frame_ctrl.cpp'
    raw=(ROOT/rel).read_text()
    body=replace_once(function(raw,'void dExtFrameCtrl_c::Advance()'),
        'void dExtFrameCtrl_c::Advance()','void _ZN9Animation7AdvanceEv(void *object)')
    body=body.replace('(int)this + 8','(int)object + 8')
    for field,offset in [('numFramesAndFlags',4),('currFrame',8),('speed',12)]:
        body=re.sub(r'\b'+field+r'\b','(*(s32 *)((char *)object+%d))'%offset,body)
    row,bindings=projected(rel,body,'',['_ZN9Animation7AdvanceEv'],auto_globals=False)
    row['unit']='stage_frame_advance'
    yield row,bindings

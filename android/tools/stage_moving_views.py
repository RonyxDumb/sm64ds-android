"""Complete moving-mesh transforms over checked cartridge-width records."""
import re
from pathlib import Path
from native_sources import function, replace_once

ROOT = Path(__file__).resolve().parents[2]


def slices(projected, free_c):
    rel = 'src/engine/collision/d_bg_w_kc_mbg.cpp'
    raw = (ROOT/rel).read_text()
    extra = '''
struct Vector3 { s32 x,y,z; };
struct RawVector3 { s32 x,y,z; };
struct RawMatrix4x3 { s32 m[12]; };
struct MovingMesh {
    u8 base[0x28]; Vector3 upAxis; u8 pad34[0x1c];
    s32 scale; Matrix4x3 mat,invRotMat,scaledMat,invMat;
    s16 angY,angVelY; Vector3 pos,velocity; u8 filterByMoveDir; u8 pad131[3];
    Matrix4x3 newScaledMat; s32 invScale;
    Matrix4x3 invScaledMat,prevInvScaledMat;
};
static_assert(sizeof(MovingMesh)==0x1c8);
static_assert(offsetof(MovingMesh,scale)==0x50);
static_assert(offsetof(MovingMesh,mat)==0x54);
static_assert(offsetof(MovingMesh,invMat)==0xe4);
static_assert(offsetof(MovingMesh,newScaledMat)==0x134);
static_assert(offsetof(MovingMesh,invScaledMat)==0x168);
'''
    specs = [
        ('void dBgW_KcMbg::SetFile(KCL_File *file, const Matrix4x3 &mtx, Fix12i scale, s16 angY,\n                         CLPS_Block &clps)',
         '_ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x3isR10CLPS_Block',
         'void *object,void *file,Matrix4x3 *matrix,s32 scale,s16 angY,void *clps'),
        ('void dBgW_KcMbg::Transform(const Matrix4x3 &mtx, s16 angle)',
         '_ZN10dBgW_KcMbg9TransformERK9Matrix4x3s',
         'void *object,Matrix4x3 *matrix,s16 angle'),
        ('int dBgW_KcMbg::TransformPos(const Vector3 &localCentre, Vector3 &res)',
         '_ZN10dBgW_KcMbg12TransformPosERK7Vector3RS0_',
         'void *object,Vector3 *centre,Vector3 *result'),
        ('s16 dBgW_KcMbg::GetAngularVelY()',
         '_ZN10dBgW_KcMbg14GetAngularVelYEv','void *object'),
        ('void dBgW_KcMbg::GetVelocity(Vector3 &res)',
         '_ZN10dBgW_KcMbg11GetVelocityER7Vector3','void *object,Vector3 *result'),
    ]
    bodies=[]
    fields=('scale','mat','invRotMat','scaledMat','invMat','angY','angVelY','pos',
            'velocity','filterByMoveDir','newScaledMat','invScale','invScaledMat',
            'prevInvScaledMat','upAxis')
    for signature,export,parameters in specs:
        body=function(raw,signature)
        body=replace_once(body,signature,signature.split()[0]+' '+export+'('+parameters+')')
        body=body.replace('dBgW_KcMbg *self = this;', 'MovingMesh *self = (MovingMesh *)object;')
        if 'MovingMesh *self' not in body:
            body=body.replace('{','{\nMovingMesh *self = (MovingMesh *)object;',1)
        body=body.replace('self->dBgW_Kc::SetFile(file, clps)',
            '_ZN7dBgW_Kc7SetFileEP8KCL_FileR10CLPS_Block(object,file,clps)')
        body=re.sub(r'\bthis\b','object',body)
        # Reference parameters become checked pointer arguments; matrix/vector
        # copies still execute all twelve/three original lanes.
        body=re.sub(r'\bmtx\b','(*matrix)',body)
        body=re.sub(r'\blocalCentre\b','(*centre)',body)
        body=re.sub(r'\bres\b','(*result)',body)
        body=body.replace('.t.x','.m[9]').replace('.t.y','.m[10]').replace('.t.z','.m[11]')
        if '::SetFile(' not in signature:
            for name in fields:
                body=re.sub(r'(?<![.>])\b'+name+r'\b','self->'+name,body)
        body=body.replace('GetVelocity(*(Vector3 *)&moveDir)',
            '_ZN10dBgW_KcMbg11GetVelocityER7Vector3(object,(Vector3 *)&moveDir)')
        bodies.append(body)
    for name in ('func_02039e48','func_02039e30','func_02039e18','func_02039db8','func_020398fc'):
        signature=re.search(r'extern "C" void '+name+r'\([^)]*\)',raw).group()
        body=function(raw,signature).replace('dBgW_KcMbg *','MovingMesh *')
        body=body.replace('.t.x','.m[9]').replace('.t.y','.m[10]').replace('.t.z','.m[11]')
        bodies.append(body)
    declarations='''
extern Matrix4x3 data_020a0e68;
void _ZN7dBgW_Kc7SetFileEP8KCL_FileR10CLPS_Block(void *,void *,void *);
void func_02039624(void *); int func_02053200(int);
int InvMat4x3(void *,void *);
void Matrix4x3_ApplyInPlaceToScale(Matrix4x3 *,s32,s32,s32);
void MulVec3Mat4x3(Vector3 *,Matrix4x3 *,Vector3 *);
int LenVec3(void *); void NormalizeVec3(void *,void *);
void func_01ffb0a4(void *); void func_01ffb07c(void *,Vector3 *);
void func_01ffb0b0(void *); void func_01ffb0bc(void *);
void func_020396d0(void *,int);
'''
    # Forward declarations for definitions in this same complete projection.
    declarations+='\n'.join('void '+name+'(MovingMesh *,Vector3 *,Vector3 *);'
        for name in ('func_02039e48','func_02039e30','func_02039e18','func_02039db8'))
    declarations+='\nvoid _ZN10dBgW_KcMbg11GetVelocityER7Vector3(void *,Vector3 *);\n'
    exports=[spec[1] for spec in specs]+['func_02039e48','func_02039e30','func_02039e18','func_02039db8','func_020398fc']
    row,bindings=projected(rel,'\n'.join(bodies),declarations,exports,extra=extra,auto_globals=False)
    row['unit']='stage_moving_mesh'
    yield row,bindings
    for rel in ('external/sm64ds-pc/src/func_02039624.c',
                'external/sm64ds-pc/src/func_020396d0.c',
                'src/Matrix4x3_ApplyInPlaceToScale.c'):
        yield free_c(rel)
    rel='src/engine/collision/d_bg_w_kc_itcm.cpp'
    raw=(ROOT/rel).read_text()
    bodies=[]
    exports=['func_01ffb0bc','func_01ffb0b0','func_01ffb0a4','func_01ffb098','func_01ffb07c']
    for name in exports:
        signature=re.search(r'extern "C" void '+name+r'\([^)]*\)',raw).group()
        bodies.append(function(raw,signature).replace('dBgW_Kc *','DepthMesh *'))
    extra='''
struct Vector3 { s32 x,y,z; };
struct DepthMesh {
    u8 base[0x34]; u8 shallowOnly,depthByDir; u8 pad36[2];
    Vector3 depthDir;
};
static_assert(offsetof(DepthMesh,depthDir)==0x38);
'''
    row,bindings=projected(rel,'\n'.join(bodies),'',exports,extra=extra,auto_globals=False)
    row['unit']='stage_moving_depth'
    yield row,bindings

// Native views of original Player members (overlay 2) whose recovered
// bodies (src/game/player/Player.cpp) use C++ member syntax the stage lift
// cannot project. Each body follows its recovered source line for line;
// offsets come from include/Player.h. Nothing here invents behavior.
#include "stage_runtime.h"
#include "packed_ptr.h"
#include <cstring>
#include <stdexcept>

namespace sm64ds {
namespace {
using packed::Ptr;
template<class T> T &at(uint32_t a) { return *Ptr<T>(a); }

// Player.h offsets.
constexpr uint32_t kBodyModels=0xdc, kHeadModels=0x154, kTexSeqPlayer=0x254;
constexpr uint32_t kParticle1=0x628, kWaterMoveParticle=0x634, kWaterStillParticle=0x638;
constexpr uint32_t kCharFileBase=0x63c, kStateFlags=0x6ce, kMegaKillCount=0x6d0;
constexpr uint32_t kPlayerNo=0x6d8, kTeleportId=0x6e8, kIsMetal=0x6f9, kIsVanish=0x6fb, kIsMega=0x703;
// Original code addresses.
constexpr uint32_t kTexSeqUpdate=0x02015988, kModelAnim2Copy=0x02016254;
constexpr uint32_t kGetBodyModelID=0x020becc8, kIsState=0x020e308c;
constexpr uint32_t kEndPowerupMusic=0x020bd984;

void end_powerup_music(uint32_t self,uint32_t music) { Ptr<void(uint32_t,uint32_t)>{kEndPowerupMusic}(self,music); }

// func_ov002_020bdef0: end the vanish cap.
void player_end_vanish(uint32_t self) {
    if (!at<uint8_t>(self+kIsVanish)) return;
    Ptr<int(uint32_t,uint32_t)>{kTexSeqUpdate}(self+kTexSeqPlayer,at<uint32_t>(self+kBodyModels+4)+8);
    Ptr<int(uint32_t,uint32_t)>{kTexSeqUpdate}(self+kTexSeqPlayer+0x14,at<uint32_t>(self+kHeadModels+4)+8);
    at<int32_t>(self+kTexSeqPlayer+8)=0;
    at<int32_t>(self+kTexSeqPlayer+0x14+8)=0;
    at<uint8_t>(self+kIsVanish)=0;
    end_powerup_music(self,0x33);
}
// func_ov002_020bdd9c: end the metal cap (restore the character's body).
void player_end_metal(uint32_t self) {
    if (at<uint8_t>(self+kIsMetal)!=1) return;
    at<uint8_t>(self+kIsMetal)=0;
    const uint32_t v=at<uint32_t>(self+8);
    const uint32_t id=Ptr<uint32_t(uint32_t,uint32_t,int)>{kGetBodyModelID}(self,v&0xff,0);
    const uint32_t file=at<uint32_t>(0x020ff480u+(at<uint32_t>(self+kCharFileBase)+v)*4);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>{kModelAnim2Copy}(
        at<uint32_t>(self+kBodyModels+id*4),at<uint32_t>(self+kBodyModels+4*4),at<uint32_t>(file+4),0);
    end_powerup_music(self,0x34);
}
// func_ov002_020bdd2c: end the mega mushroom.
void player_end_mega(uint32_t self) {
    if (at<uint8_t>(self+kIsMega)==1) {
        Ptr<void(uint32_t,uint32_t)>(0x020db8bcu)(self,3);
        at<uint32_t>(at<uint32_t>(0x0209f318u)+0x154)&=~0x80u; // dCamera_c::mFlags
        end_powerup_music(self,0x31);
        at<int32_t>(self+kWaterStillParticle)=0;
        at<int32_t>(self+kWaterMoveParticle)=0;
        at<int32_t>(self+kParticle1)=0;
        at<uint16_t>(self+kMegaKillCount)=0;
    }
    at<uint8_t>(self+kIsMega)=0;
}
// func_ov002_020c2e78: start a pending warp.
void player_check_warp(uint32_t self) {
    const uint8_t w=Ptr<uint8_t(int)>(0x02020414u)(at<uint8_t>(self+kPlayerNo));
    if (!w) return;
    if (!Ptr<int(uint32_t,uint32_t)>{kIsState}(self,0x02110154u)) {
        Ptr<void(int)>(0x02020388u)(at<uint8_t>(self+kPlayerNo));
        return;
    }
    at<uint8_t>(self+kTeleportId)=w;
    at<uint16_t>(self+kStateFlags)|=8;
}
// func_ov002_020bfa74: conveyor/current surfaces (CLPS type 0xb). The
// entry test is the recovered body; the push path (PathPtr nodes, flow
// particle, velocity bend) is not ported yet and fails explicitly.
int player_conveyor_push(uint32_t self) {
    if (at<int32_t>(self+0x664)!=0xb) return 0;
    if (at<int32_t>(self+0x670)==0xff) return 0;
    throw std::runtime_error("Unported original path: func_ov002_020bfa74 conveyor push.");
}
// Player::Unk_020c9e5c(u8 kind): in the no-control state (0x0211022c) with
// mNoCtrlKind (0x70a) == kind, is mStateStep (0x6e3) at the kind's release step?
int player_no_control_step(uint32_t self,uint32_t kind) {
    const uint8_t h=uint8_t(kind);
    if (!Ptr<int(uint32_t,uint32_t)>{kIsState}(self,0x0211022cu)) return 0;
    if (at<uint8_t>(self+0x70a)!=h) return 0;
    const uint8_t s=at<uint8_t>(self+0x6e3);
    const auto in=[s](uint8_t base,uint8_t span) { return uint8_t(s-base)<=span; };
    switch (h) {
    case 0: case 3: return s==1;
    case 0x11: if (s==0x18) return 1; [[fallthrough]];
    case 1: case 2: return s==2 || in(0x14,1);
    case 4: return s==3;
    case 5: return in(4,1);
    case 6: return s==6;
    case 7: return in(7,1);
    case 8: return s==9 || in(0x14,1);
    case 9: return in(0xb,2);
    case 0xa: case 0xc: return in(0xe,1);
    case 0xb: return s==0x10;
    case 0xd: return s==0x11;
    case 0xe: return s==0x12;
    case 0xf: return in(0x16,1);
    case 0x10: return in(0x18,1);
    }
    return 0;
}
// SurfaceInfo::CopyNormalTo(Vector3 &) const: the normal at 0x08..0x13.
void surface_copy_normal(uint32_t self,uint32_t out) {
    for (unsigned lane=0;lane<3;++lane) at<int32_t>(out+lane*4)=at<int32_t>(self+8+lane*4);
}
// dBgCh_SphCrr::SetObjAndSphere(const Vector3 &, Fix12i, dActor_c *)
// (0x02037c00; recovered unit, checked against the cartridge code): the
// dM3dGSph base at 0x38, owner, reset, and 0x1000 at 0x108.
void sphcrr_set_obj_and_sphere(uint32_t self,uint32_t pos,int32_t radius,uint32_t actor) {
    Ptr<void(uint32_t,uint32_t,int32_t)>{0x0203abd4u}(self+0x38,pos,radius);
    Ptr<void(uint32_t,uint32_t)>{0x020353b0u}(self,actor);
    Ptr<void(uint32_t)>{0x02037b5cu}(self);
    at<int32_t>(self+0x108)=0x1000;
}
// func_020371fc (recovered isolated unit + cartridge layout): snap a
// grounded actor down onto a nearby floor below a steep wall contact. The
// local dBgCh_Gnd (0x50) has its dBgPi base at +0x10 and clsnY at +0x44.
void bgch_actr_snap_to_floor(uint32_t self) {
    auto &space=packed::Space::current();
    const uint8_t f=at<uint8_t>(self+0x90);
    if (!(f&1) || !(f&8) || !(at<int32_t>(self+0x10)&0x100) || at<int32_t>(self+0x1b0)>=0xf20) return;
    const uint32_t actor=at<uint32_t>(self+0x14);
    if (Ptr<int(uint32_t)>{0x020c2f3cu}(actor)) return; // Player::IsInAir
    alignas(8) uint8_t buffer[0x50];
    const uint32_t rg=space.address(buffer),objpos=actor+0x5c;
    int32_t pos[3]={at<int32_t>(objpos),at<int32_t>(objpos+4)+at<int32_t>(self+0x18),at<int32_t>(objpos+8)};
    Ptr<uint32_t(uint32_t)>{0x02037570u}(rg);
    Ptr<void(uint32_t,uint32_t,uint32_t)>{0x0203748cu}(rg,space.address(pos),actor);
    if (Ptr<int(uint32_t)>{0x02038f44u}(rg)) {
        const int32_t cy=at<int32_t>(rg+0x44),diff=at<int32_t>(objpos+4)-cy;
        if (diff>0 && diff<(at<int32_t>(self+0x18)<<1)) {
            at<int32_t>(objpos+4)=cy;
            at<uint8_t>(self+0x90)|=4;
            Ptr<void(uint32_t,uint32_t)>{0x02038018u}(self+0x94,rg+0x10);
            Ptr<void(uint32_t)>{0x02035708u}(self); // SetGroundFlag
        }
    }
    Ptr<uint32_t(uint32_t)>{0x02037534u}(rg);
}
// Constructors/destructors of the sphere query and its bases, from the
// cartridge code: dBgCh (vptr 0x020991d8), dBgPi (0x02099368, dBgPc at +4),
// dM3dGSph (0x020994cc); dBgCh_SphCrr = dBgCh + dBgPi@0x10 + dM3dGSph@0x38
// with vptrs 0x02099338/48/58 and three dBgPi results at 0x74/0x9c/0xc4.
uint32_t bgch_construct2(uint32_t self) {
    at<uint32_t>(self)=0x020991d8u; at<uint8_t>(self+4)=1; at<uint32_t>(self+0xc)=0; at<int32_t>(self+8)=-1;
    return self;
}
uint32_t bgch_destruct2(uint32_t self) { at<uint32_t>(self)=0x020991d8u; return self; }
uint32_t m3dgsph_vptr(uint32_t self) { at<uint32_t>(self)=0x020994ccu; return self; }
uint32_t sphcrr_construct(uint32_t self) {
    bgch_construct2(self);
    Ptr<uint32_t(uint32_t)>{0x0203816cu}(self+0x10); // dBgPi C2 == C1 body
    m3dgsph_vptr(self+0x38);
    at<uint32_t>(self)=0x02099338u; at<uint32_t>(self+0x10)=0x02099348u; at<uint32_t>(self+0x38)=0x02099358u;
    for (uint32_t r : {0x74u,0x9cu,0xc4u}) Ptr<uint32_t(uint32_t)>{0x0203816cu}(self+r);
    at<uint32_t>(self+0xec)=0;
    return self;
}
uint32_t sphcrr_destruct(uint32_t self) {
    at<uint32_t>(self)=0x02099338u; at<uint32_t>(self+0x10)=0x02099348u; at<uint32_t>(self+0x38)=0x02099358u;
    for (uint32_t r : {0xc4u,0x9cu,0x74u}) Ptr<uint32_t(uint32_t)>{0x02038144u}(self+r);
    m3dgsph_vptr(self+0x38);
    Ptr<uint32_t(uint32_t)>{0x02038144u}(self+0x10); // dBgPi D2 == D1 body
    bgch_destruct2(self);
    return self;
}
// dBgW_KcMbg::DetectClsn(dBgCh_SphCrr &) (0x02039a3c; recovered isolated
// unit, laid out against the cartridge code): a local dBgCh_SphCrr (0x110)
// in the mesh's frame (invScale 0x164), the KCL sphere test, results scaled
// back (scale 0x50) and merged into the caller's sphere.
int mbg_detect_sphere(uint32_t self,uint32_t sphere) {
    auto &space=packed::Space::current();
    const auto fmul=[](int32_t a,int32_t b) { return int32_t((int64_t(a)*b+0x800)>>12); };
    alignas(8) uint8_t buffer[0x110];
    int32_t pos[3],d[12];
    const uint32_t loc=space.address(buffer);
    Ptr<uint32_t(uint32_t)>{0x02037d18u}(loc); // dBgCh_SphCrr C1
    Ptr<void(uint32_t,uint32_t,uint32_t)>{0x02039e48u}(self,sphere+0x3c,space.address(pos));
    const int32_t inv=at<int32_t>(self+0x164);
    Ptr<void(uint32_t,uint32_t,int32_t,uint32_t)>{0x02037c00u}(loc,space.address(pos),fmul(at<int32_t>(sphere+0x48),inv),0);
    at<int32_t>(loc+0xec)=fmul(at<int32_t>(sphere+0xec),inv);
    Ptr<void(uint32_t,uint32_t)>{0x02037940u}(loc,at<uint8_t>(sphere+0x70));
    Ptr<void(uint32_t,uint32_t)>{0x02035394u}(loc,sphere);
    int r=Ptr<int(uint32_t,uint32_t)>{0x01ffb830u}(self,loc);
    if (r) {
        Ptr<void(uint32_t,uint32_t,uint32_t)>{0x02037a04u}(loc,space.address(d),space.address(d+3));
        const int32_t scale=at<int32_t>(self+0x50);
        for (int i=0;i<6;++i) d[6+i]=fmul(d[i],scale);
        Ptr<void(uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t)>{0x02037a6cu}(sphere,d[6],d[7],d[8],d[9],d[10],d[11]);
        Ptr<void(uint32_t,uint32_t)>{0x02038018u}(sphere+0x10,loc+0x10); // dBgPi::operator=
        auto &flags=at<uint8_t>(sphere+0x70);
        flags|=1;
        const uint8_t locFlags=at<uint8_t>(loc+0x70);
        if (locFlags&4) {
            if (flags&4) r&=~1;
            else Ptr<void(uint32_t,uint32_t)>{0x020378e4u}(sphere,loc+0x74); // SetFloorResult
            flags|=4;
            if (at<int32_t>(sphere+0x100)<at<int32_t>(loc+0x100)) Ptr<void(uint32_t,uint32_t)>{0x0203794cu}(sphere,loc+0xfc);
        }
        if (locFlags&8) { Ptr<void(uint32_t,uint32_t)>{0x02037888u}(sphere,loc+0x9c); flags|=8; }
        if (locFlags&0x10) { Ptr<void(uint32_t,uint32_t)>{0x0203782cu}(sphere,loc+0xc4); flags|=0x10; }
    }
    Ptr<uint32_t(uint32_t)>{0x02037cb0u}(loc); // dBgCh_SphCrr D1
    return r;
}
// dBgCh_SphCrr::DetectClsn (0x02038b78; recovered isolated unit): the
// sphere against the stage mesh (data_020a0c80[0]) and every registered
// moving mesh in range; vtable slot 8 returns the hit mask.
int sphcrr_detect(uint32_t self) {
    auto &space=packed::Space::current();
    const auto meshInfo=[](uint32_t o) { return Ptr<uint32_t(uint32_t)>{0x020393b4u}(o); };
    const auto meshId=[](uint32_t o) { return Ptr<uint32_t(uint32_t)>{0x020393acu}(o); };
    const auto test=[self](uint32_t o) {
        return Ptr<int(uint32_t,uint32_t)>{at<uint32_t>(at<uint32_t>(o)+8*4)}(o,self);
    };
    const auto record=[&](uint32_t o,int i,int mask) {
        using Rec=void(uint32_t,int,uint32_t,uint32_t,uint32_t);
        Ptr<Rec>{0x02037fecu}(self+0x10,i,meshId(o),meshInfo(o),o);
        if (mask&1) Ptr<Rec>{0x020379d0u}(self,i,meshId(o),meshInfo(o),o);
        if (mask&2) Ptr<Rec>{0x0203799cu}(self,i,meshId(o),meshInfo(o),o);
        if (mask&4) Ptr<Rec>{0x02037968u}(self,i,meshId(o),meshInfo(o),o);
    };
    const auto excluded=[self](uint32_t info) { return Ptr<int(uint32_t,uint32_t)>{0x02035354u}(self,info)!=0; };
    int result=0;
    if (const uint32_t e=at<uint32_t>(0x020a0c80u)) {
        if (!excluded(meshInfo(e))) {
            if (const int mask=test(e)) { record(e,0,mask); result=1; }
        }
    }
    const uint32_t centre=self+0x3c;
    const int32_t threshold=at<int32_t>(self+0x48)+0x1000;
    for (int i=1;i<0x18;++i) {
        const uint32_t o=at<uint32_t>(0x020a0c80u+i*4);
        if (!o) continue;
        const uint32_t info=meshInfo(o);
        if (excluded(info)) continue;
        if (info && (at<int32_t>(info+0xb0)&2)) {
            int32_t v[3]={at<int32_t>(info+0x5c),at<int32_t>(info+0x60),at<int32_t>(info+0x64)};
            int mask=Ptr<int(uint32_t)>{0x0203939cu}(o);
            if (mask==-0x1000) { v[1]+=at<int32_t>(info+0xb4); mask=at<int32_t>(info+0xb8)<<3; }
            else v[1]+=Ptr<int(uint32_t)>{0x0203938cu}(o);
            if (Ptr<int(uint32_t,uint32_t)>{0x0203cfdcu}(centre,space.address(v))>mask+threshold) continue;
        }
        if (const int mask=test(o)) { record(o,i,mask); result=1; }
    }
    Ptr<void(uint32_t)>{0x02037a38u}(self);
    return result;
}
// func_0203859c (recovered isolated unit): test the line query against the
// first registered collision mesh (data_020a0c80[0]) through its vtable slot 7.
int bg_line_first_mesh(uint32_t obj) {
    const uint32_t e=at<uint32_t>(0x020a0c80u);
    if (!e) return 0;
    if (Ptr<int(uint32_t,uint32_t)>{0x02035354u}(obj,Ptr<uint32_t(uint32_t)>{0x020393b4u}(e))!=0) return 0;
    if (!Ptr<int(uint32_t,uint32_t)>{at<uint32_t>(at<uint32_t>(e)+7*4)}(e,obj)) return 0;
    const uint32_t a=Ptr<uint32_t(uint32_t)>{0x020393acu}(e),b=Ptr<uint32_t(uint32_t)>{0x020393b4u}(e);
    Ptr<void(uint32_t,int,uint32_t,uint32_t,uint32_t)>{0x02037fecu}(obj+0x10,0,a,b,e);
    return 1;
}
// dBgPi::dBgPi (0x0203816c, from the cartridge code): the dBgPc C2 body on
// the member at +4 (0xfc0, 0xff, then three zero words), the vptr, then
// func_020380c0 (its reset).
uint32_t bgpi_construct(uint32_t self) {
    const uint32_t pc=self+4;
    at<uint32_t>(pc)=0xfc0; at<uint32_t>(pc+4)=0xff;
    at<uint32_t>(pc+0x10)=0; at<uint32_t>(pc+0xc)=0; at<uint32_t>(pc+8)=0;
    at<uint32_t>(self)=0x02099368u;
    Ptr<void(uint32_t)>{0x020380c0u}(self);
    return self;
}
// dBgPi::~dBgPi (0x02038144): its vptr, then the dBgPc member at +4
// (dBgPc D2 is an empty 4-byte body, so nothing else happens).
uint32_t bgpi_destruct(uint32_t self) {
    at<uint32_t>(self)=0x02099368u; // _ZTV5dBgPi
    return self;
}
// dBgCh_Actr (dBgCh_Actr.h): mSphereClsn 0x20 (flags at 0x90),
// mRaycastLine 0x134. Recovered one-line bodies.
void bgch_actr_stop_water(uint32_t self) {
    Ptr<void(uint32_t)>{0x02035488u}(self+0x134); // dBgCh::StopDetectingWater
    Ptr<void(uint32_t)>{0x02035488u}(self+0x20);
}
int bgch_actr_wall_result(uint32_t self) { return Ptr<int(uint32_t)>{0x020378dcu}(self+0x20); }
int bgch_actr_floor_result(uint32_t self) { return Ptr<int(uint32_t)>{0x02037938u}(self+0x20); }
int bgch_actr_on_wall(uint32_t self) { return at<uint8_t>(self+0x90)&8; }
}

namespace {
template<uint32_t Target> uint32_t tail_veneer(uint32_t r0,uint32_t r1,uint32_t r2,uint32_t r3) {
    return Ptr<uint32_t(uint32_t,uint32_t,uint32_t,uint32_t)>{Target}(r0,r1,r2,r3);
}
}
void bind_stage_tail_veneers(packed::Space &space) {
#define VENEER(veneer, target) space.bind(veneer,&tail_veneer<target>);
#include "stage_tail_veneers.inc"
#undef VENEER
}

void bind_stage_player_runtime(packed::Space &space) {
    space.bind(0x02035820u,&bgch_actr_stop_water);
    space.bind(0x0203565cu,&bgch_actr_wall_result);
    space.bind(0x0203566cu,&bgch_actr_floor_result);
    space.bind(0x0203562cu,&bgch_actr_on_wall);
    space.bind(0x02038144u,&bgpi_destruct);
    space.bind(0x0203816cu,&bgpi_construct);
    space.bind(0x0203859cu,&bg_line_first_mesh);
    space.bind(0x02037c00u,&sphcrr_set_obj_and_sphere);
    space.bind(0x02038b78u,&sphcrr_detect);
    space.bind(0x02039a3cu,&mbg_detect_sphere);
    space.bind(0x02035514u,&bgch_construct2);
    space.bind(0x020354d0u,&bgch_destruct2);
    space.bind(0x0203ac70u,&m3dgsph_vptr);
    space.bind(0x0203ac1cu,&m3dgsph_vptr);
    space.bind(0x02037d18u,&sphcrr_construct);
    space.bind(0x020371fcu,&bgch_actr_snap_to_floor);
    space.bind(0x02037cb0u,&sphcrr_destruct);
    space.bind(0x02037dccu,&surface_copy_normal);
    space.bind(0x020c9e5cu,&player_no_control_step);
    space.bind(0x020bfa74u,&player_conveyor_push);
    space.bind(0x020bdef0u,&player_end_vanish);
    space.bind(0x020bdd9cu,&player_end_metal);
    space.bind(0x020bdd2cu,&player_end_mega);
    space.bind(0x020c2e78u,&player_check_warp);
}
}

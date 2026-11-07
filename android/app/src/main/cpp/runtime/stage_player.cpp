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
    space.bind(0x02037dccu,&surface_copy_normal);
    space.bind(0x020c9e5cu,&player_no_control_step);
    space.bind(0x020bfa74u,&player_conveyor_push);
    space.bind(0x020bdef0u,&player_end_vanish);
    space.bind(0x020bdd9cu,&player_end_metal);
    space.bind(0x020bdd2cu,&player_end_mega);
    space.bind(0x020c2e78u,&player_check_warp);
}
}

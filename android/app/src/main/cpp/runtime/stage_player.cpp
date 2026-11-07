// Native views of original Player members (overlay 2) whose recovered
// bodies (src/game/player/Player.cpp) use C++ member syntax the stage lift
// cannot project. Each body follows its recovered source line for line;
// offsets come from include/Player.h. Nothing here invents behavior.
#include "stage_runtime.h"
#include "packed_ptr.h"
#include <cstring>

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
}

void bind_stage_player_runtime(packed::Space &space) {
    space.bind(0x020bdef0u,&player_end_vanish);
    space.bind(0x020bdd9cu,&player_end_metal);
    space.bind(0x020bdd2cu,&player_end_mega);
    space.bind(0x020c2e78u,&player_check_warp);
}
}

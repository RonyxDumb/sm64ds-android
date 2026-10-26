#include "stage_flow.h"
#include "resources.h"
#include "PlayerInput.h"
#include "nitro/fx/fx.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace cstd {
int sqrt(unsigned long long);
s16 atan2(s32, s32);
s32 fdiv(s32, s32);
}

namespace {
u16 buttonMaps[3][16]{};
u32 little32(const u8 *p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
}

extern "C" {
s8 SUBLEVEL_LEVEL_TABLE[0x34];
s8 data_02092110 = -1, data_02092118 = -1, data_0209211c = -1, data_0209f2f8 = 0;
s8 data_02092124 = 0, data_0209f2f4 = 0;
u8 data_02092114 = 0, data_02092128[4]{}, data_0208a0e0 = 1;
s16 data_02092144[4]{};
u8 data_0209f1f0 = 0, data_0209f220 = 0, data_0209f26c = 0, data_0209f200 = 0;
u8 data_0209f268 = 0, data_0209f24c = 0, data_0209f270 = 0, data_0209f2d8 = 0, data_0209f254 = 0;
u8 data_0209f228[4]{};
sm64ds::ControllerInput data_0209f498[4]{};
// InitControllerMode's byte-array spelling aliases the exact +0x16 lane of
// ControllerInput. A separate array here would silently desynchronise Player.
asm(".global data_0209f4ae\n.set data_0209f4ae, data_0209f498 + 0x16\n");
u8 data_0209fc50 = 0, data_0209fc5c[4]{};
char data_0209fc64[4]{};
u8 data_0209f21c = 0, data_0209f350[4]{}, data_0209fc48 = 0;
u8 data_0209f294 = 0, data_0209f2c4 = 0, data_0209f20c = 0, data_020a1166[4 * 0x24]{};
u16 data_0209f368[4]{};
s16 data_020756b0[16]{}, data_02082214[8192]{};
u16 *data_0209214c[3] = {buttonMaps[0], buttonMaps[1], buttonMaps[2]};
u16 data_020994e0[1025]{};
u8 data_ov002_02111180 = 0;
}

namespace sm64ds {
StageFlow::StageFlow(Cartridge &cartridge) : cartridge_(cartridge) {
    auto table = cartridge.arm9(0x02075298, sizeof SUBLEVEL_LEVEL_TABLE);
    std::memcpy(SUBLEVEL_LEVEL_TABLE, table.data(), table.size());
    for (auto span : {std::make_pair(0x020756b0u, std::make_pair(static_cast<void *>(data_020756b0),
                                                                 sizeof data_020756b0)),
                      std::make_pair(0x02082214u, std::make_pair(static_cast<void *>(data_02082214),
                                                                 sizeof data_02082214)),
                      std::make_pair(0x020994e0u, std::make_pair(static_cast<void *>(data_020994e0),
                                                                 sizeof data_020994e0))}) {
        auto bytes = cartridge.arm9(span.first, span.second.second);
        std::memcpy(span.second.first, bytes.data(), bytes.size());
    }
    // ROM pointer cells are decoded as addresses into immutable cartridge data.
    // The live table contains genuine native pointers to native-owned copies.
    auto maps = cartridge.arm9(0x0209214c, 3 * 4);
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto bytes = cartridge.arm9(little32(maps.data() + mode * 4), sizeof buttonMaps[mode]);
        std::memcpy(buttonMaps[mode], bytes.data(), bytes.size());
    }
    data_02092110 = data_02092118 = data_0209211c = -1;
    data_0209f2f8 = 0;
    data_02092124 = data_0209f2f4 = 0;
    data_02092114 = data_0209f1f0 = data_0209f220 = data_0209f26c = data_0209f200 = 0;
    data_0209f268 = data_0209f24c = data_0209f270 = data_0209f2d8 = data_0209f254 = 0;
    std::memset(data_02092128, 0, sizeof data_02092128);
    std::memset(data_02092144, 0, sizeof data_02092144);
    std::memset(data_0209f228, 0, sizeof data_0209f228);
    std::memset(data_0209f498, 0, sizeof data_0209f498);
    std::memset(data_0209fc5c, 0, sizeof data_0209fc5c);
    std::memset(data_0209fc64, -1, sizeof data_0209fc64);
    std::memset(data_0209f350, 0, sizeof data_0209f350);
    std::memset(data_0209f368, 0, sizeof data_0209f368);
    std::memset(data_020a1166, 0, sizeof data_020a1166);
    data_0209f294 = data_0209f2c4 = data_0209f20c = data_0209f21c = data_0209fc48 = 0;
    data_ov002_02111180 = 0;
    data_0208a0e0 = 1;
    data_0209fc50 = 0;
}

const ControllerInput &StageFlow::controller(unsigned slot) const {
    if (slot >= 4)
        throw std::out_of_range("Invalid stage controller slot.");
    return data_0209f498[slot];
}

void StageFlow::prepare_scene() {
    if (data_02092110 < 0 || data_02092110 >= 0x34)
        throw std::runtime_error("A save file requested an invalid native sublevel.");
    // The game's StartFile has already set the level/entrance and requested
    // dScStage_c. Mount the same overlay and level model/KCL the original
    // InitResources reads. The class lifecycle and actor spawn must consume
    // these data; this does NOT substitute for them.
    auto nextAssets = std::make_unique<StageAssets>(
        StageAssets::load(cartridge_, static_cast<uint8_t>(data_02092110)));
    auto nextArm9 = std::make_unique<Cartridge::DataImage>(
        cartridge_.arm9_data(nextAssets->overlay().base));
    auto nextSpace = std::make_unique<packed::Space>(Resources::active().heap());
    nextSpace->map(nextArm9->base, nextArm9->bytes.data(), nextArm9->bytes.size());
    nextSpace->map(nextAssets->overlay().base, nextAssets->overlay().bytes.data(),
                   nextAssets->overlay().bytes.size());
    // The original LVL_Overlay and its CLPS/object pointers must round-trip
    // through the same checked 32-bit address space the actor ABI consumes.
    if (nextSpace->address(nextAssets->overlay_at(
            nextAssets->descriptor().levelAddress, 0x1c)) !=
        nextAssets->descriptor().levelAddress)
        throw std::runtime_error("Native stage overlay cannot be rebound to DS addresses.");
    // Destroy an old address space before its backing buffers, then install
    // the next scene atomically once all cartridge and ABI checks pass.
    addressSpace_.reset();
    assets_ = std::move(nextAssets);
    arm9Image_ = std::move(nextArm9);
    addressSpace_ = std::move(nextSpace);
}

void StageFlow::prepare_input() {
    if (data_0208a0e0 > 4)
        throw std::runtime_error("Invalid native player count.");
    data_0209f21c = data_0208a0e0;
    reset_input();
}

void StageFlow::update_input(u32 cameraFlags, float stickX, float stickY) {
    if (data_0209f21c > 4)
        throw std::runtime_error("Invalid native player count.");
    for (unsigned slot = 0; slot < data_0209f21c; ++slot)
        if (data_0209f498[slot].mode > 2)
            throw std::runtime_error("Invalid controller mode in the selected save.");
    check_camera_input(cameraFlags);
    check_input();
    // Android sticks feed the same Q12 record that the recovered Player reads.
    // Menu key emulation remains digital; gameplay retains partial deflection.
    // Keep the source's cutscene/input gate and an active DS stylus gesture.
    if (!inputEnabled_ || !data_0209f21c || data_0209f498[0].touching || !std::isfinite(stickX) ||
        !std::isfinite(stickY))
        return;
    s32 x = std::lround(std::clamp(stickX, -1.f, 1.f) * 4096);
    s32 y = std::lround(std::clamp(stickY, -1.f, 1.f) * 4096);
    if (!x && !y)
        return;
    s32 magnitude = cstd::sqrt(s64(x) * x + s64(y) * y);
    if (magnitude > 4096) {
        const s32 scale = cstd::fdiv(4096, magnitude);
        x = FX_MUL(x, scale);
        y = FX_MUL(y, scale);
        magnitude = 4096;
    }
    auto &control = data_0209f498[0];
    control.nx = x;
    control.ny = y;
    control.mag = magnitude;
    control.ang = cstd::atan2(x, y);
}

StageRequest StageFlow::request() const {
    return {data_02092110,    data_0209f268, data_0209f1f0,
            data_02092128[0], data_0208a0e0, controller(0).mode};
}
}

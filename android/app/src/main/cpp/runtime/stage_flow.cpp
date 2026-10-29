#include "stage_flow.h"
#include "stage_runtime.h"
#include "packed_fs.h"
#include "boot_seams.h"
#include "collision_dispatch.h"
#include "resources.h"
#include "packed_ptr.h"
#include "PlayerInput.h"
#include "nitro/fx/fx.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

extern "C" void Vec3_MulScalarInPlace(int *v, int scale);

namespace cstd {
int sqrt(unsigned long long);
s16 atan2(s32, s32);
s32 fdiv(s32, s32);
}

namespace sm64ds::frontend { void bind_sources(packed::Space &); }
namespace sm64ds::stage_sources { void bind_sources(packed::Space &); }

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
    auto nextCommon = std::make_unique<Cartridge::DataImage>(cartridge_.overlay_data(1));
    // The original ARM9 BSS ends before the *first* resident overlay.
    // Mapping it to the level overlay would swallow overlay 2's addresses
    // and collide with the actor dispatch/ID tables on ARM64.
    const uint32_t overlayBoundary = std::min(
        {nextCommon->base,nextAssets->overlay().base,nextAssets->object_overlay().base});
    auto nextArm9 = std::make_unique<Cartridge::DataImage>(
        cartridge_.arm9_data(overlayBoundary));
    auto nextSpace = std::make_unique<packed::Space>(Resources::active().heap());
    nextSpace->map(nextArm9->base, nextArm9->bytes.data(), nextArm9->bytes.size());
    nextSpace->map(nextCommon->base,nextCommon->bytes.data(),nextCommon->bytes.size());
    nextSpace->map(nextAssets->overlay().base, nextAssets->overlay().bytes.data(),
                   nextAssets->overlay().bytes.size());
    // Overlay 2 holds the actor factory and category dispatch data used by
    // LoadClsnAndObjects. It is a separate, simultaneously resident DS image
    // and must resolve through the same checked 32-bit ABI address space.
    const auto &objectOverlay = nextAssets->object_overlay();
    nextSpace->map(objectOverlay.base, const_cast<uint8_t *>(objectOverlay.bytes.data()),
                   objectOverlay.bytes.size());
    for (auto span : {std::make_pair(ntr::IO_BASE,ntr::IO_SIZE),
                      std::make_pair(ntr::PLTT_BASE,ntr::PLTT_SIZE),
                      std::make_pair(ntr::VRAM_BASE,ntr::VRAM_SIZE),
                      std::make_pair(ntr::OAM_BASE,ntr::OAM_SIZE)})
        nextSpace->map(static_cast<uint32_t>(span.first),
                       ntr::host_ptr(static_cast<uint32_t>(span.first)),span.second);
    // Reuse the real checked SDK/model/particle functions already linked for
    // the title. Stage methods have their own complete-source ABI bindings.
    frontend::bind_sources(*nextSpace);
    stage_sources::bind_sources(*nextSpace);
    // src/unnamed/arm9/0201/func_0201a054.c installs this very table at
    // startup. Keep its cartridge words intact; factories resolve only via
    // explicitly registered native code in the stage's address space.
    const uint32_t spawnTable = 0x02090864u; // ACTOR_SPAWN_TABLE, EU symbol map
    std::memcpy(nextSpace->data(0x020a4bb8u, 4), &spawnTable, 4);
    // Actual recovered fBase_c methods. Bind original ARM9 addresses from
    // config/arm9/symbols.txt to host-native implementations. Unported
    // subclass methods still cause an explicit missing-code failure.
    nextSpace->bind(0x02043c80u, &native_fbase_init);
    nextSpace->bind(0x02043b24u, &native_fbase_behavior);
    nextSpace->bind(0x02043af0u, &native_fbase_render);
    nextSpace->bind(0x02043afcu, &native_fbase_before_behavior);
    nextSpace->bind(0x02043ac8u, &native_fbase_before_render);
    nextSpace->bind(0x02043fdcu, &native_fbase_process_list);
    // dActor_c recovered movement/clip routines (original ARM9 symbols).
    nextSpace->bind(0x02010e08u, &native_actor_set_ranges);
    nextSpace->bind(0x02010da4u, &native_actor_clamp_pos);
    nextSpace->bind(0x02010c5cu, &native_actor_update_horz_speed);
    nextSpace->bind(0x02010d40u, &native_actor_update_pos_only_speed);
    nextSpace->bind(0x02010c30u, &native_actor_update_pos);
    // dCc_c source methods: native collision-list ownership and reset.
    nextSpace->bind(0x02015040u, &native_collision_init);
    nextSpace->bind(0x02015024u, &native_collision_clear);
    nextSpace->bind(0x02014ff0u, &native_collision_update);
    nextSpace->bind(0x02014fa4u, &native_collision_unlink);
    nextSpace->bind(0x02014aa8u, &native_collision_process);
    bind_stage_runtime(*nextSpace);
    bind_packed_fs(*nextSpace);
    initialize_packed_fs(*nextSpace);
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
    commonOverlay_ = std::move(nextCommon);
    addressSpace_ = std::move(nextSpace);
    sync_state(true);
    sceneActor_ = 0;
    sceneInitialized_ = false;
}

uint32_t StageFlow::spawn_scene(uint32_t sceneID, uint32_t parameter, uint8_t kind) {
    if (!addressSpace_)
        throw std::runtime_error("Scene spawn before original stage resources are loaded.");
    packed::Space::Scope scope(*addressSpace_);
    using packed::Ptr;
    // Source chain: func_02013edc -> func_02042fe4 -> func_02043098.
    // Expose the live r0 return value which the recovered void veneers lose.
    // func_02043060's optional pre-spawn hook consumes the scene ID.
    *Ptr<uint16_t>(0x020a4b50u) = static_cast<uint16_t>(sceneID);
    *Ptr<uint8_t>(0x020a4b4cu) = 2;
    const uint32_t hook = *Ptr<uint32_t>(0x020a4b58u);
    if (hook && Ptr<int(uint32_t)>(hook)(sceneID) == 3)
        return 0;
    *Ptr<uint8_t>(0x020a4b4cu) = 3;
    // func_02043180(sceneID, parent=0, parameter, kind).
    *Ptr<uint32_t>(0x020a4b60u) = parameter;
    *Ptr<uint16_t>(0x020a4b54u) = static_cast<uint16_t>(sceneID);
    *Ptr<uint8_t>(0x020a4b48u) = kind;
    *Ptr<uint32_t>(0x020a4b64u) = 0;
    *Ptr<uint8_t>(0x020a4b4cu) = 4;
    const uint32_t table = *Ptr<uint32_t>(0x020a4bb8u);
    if (uint64_t(table) + uint64_t(sceneID) * 4 > UINT32_MAX)
        throw std::runtime_error("Scene profile index overflows its cartridge address.");
    const uint32_t profile = *Ptr<uint32_t>(table + sceneID * 4);
    const uint32_t factory = *Ptr<uint32_t>(profile);
    // A missing factory now fails at its actual native-code binding, rather
    // than an unconditional stop at every scene-3 transition. Never execute
    // a cartridge ARM address or replace an unported class with a dummy actor.
    const uint32_t actor = Ptr<uint32_t()>(factory)();
    if (!actor) {
        *Ptr<uint8_t>(0x020a4b4cu) = 0;
        *Ptr<uint16_t>(0x020a4b50u) = 0xffff;
        return 0;
    }
    (void)addressSpace_->data(actor, 0x50);
    *Ptr<uint8_t>(0x020a4b4cu) = 5;
    Ptr<void(uint32_t)>(0x020433b8u)(actor); // original post-construction scheduler hook
    *Ptr<uint8_t>(0x020a4b4cu) = 0;
    *Ptr<uint16_t>(0x020a4b50u) = 0xffff;
    sceneActor_ = actor;
    // The original post-construction scheduler already runs InitResources.
    // A successful actor has aliveState == 1 and must not initialize twice.
    sceneInitialized_ = *Ptr<uint8_t>(actor+0x0e) == 1;
    sync_state(false);
    return actor;
}

void StageFlow::update_scene() {
    if (!sceneActor_ || !addressSpace_)
        throw std::runtime_error("Scene tick without an original scene actor.");
    packed::Space::Scope scope(*addressSpace_);
    sync_state(true);
    ActorDispatch dispatch(*addressSpace_);
    if (!sceneInitialized_) {
        const int result = dispatch.process(sceneActor_, {{0,1}, {4,1}, {8,1}});
        if (result != 1) { sync_state(false); return; }
        sceneInitialized_ = true;
    }
    dispatch.process(sceneActor_, {{24,1}, {28,1}, {32,1}});
    dispatch.process(sceneActor_, {{36,1}, {40,1}, {44,1}});
    sync_state(false);
}

void StageFlow::sync_state(bool toPacked) {
    // Only native-owned boot/input/save lanes are marshalled. Every other
    // stage global and all pointer tables remain in the cartridge data image.
    // Aliasing a widened native object into that image would corrupt offsets.
    auto copy=[&](uint32_t address,void *native,size_t bytes) {
        void *record=addressSpace_->data(address,bytes);
        if (toPacked) std::memcpy(record,native,bytes);
        else std::memcpy(native,record,bytes);
    };
#define SYNC_STAGE(name, address) copy(address,&name,sizeof name)
    SYNC_STAGE(data_0208a0e0,0x0208a0e0u);
    SYNC_STAGE(data_02092110,0x02092110u);
    SYNC_STAGE(data_02092118,0x02092118u);
    SYNC_STAGE(data_0209211c,0x0209211cu);
    SYNC_STAGE(data_02092124,0x02092124u);
    SYNC_STAGE(data_02092114,0x02092114u);
    SYNC_STAGE(data_02092128,0x02092128u);
    SYNC_STAGE(data_02092144,0x02092144u);
    SYNC_STAGE(data_0209f2f8,0x0209f2f8u);
    SYNC_STAGE(data_0209f2f4,0x0209f2f4u);
    SYNC_STAGE(data_0209f1f0,0x0209f1f0u);
    SYNC_STAGE(data_0209f220,0x0209f220u);
    SYNC_STAGE(data_0209f26c,0x0209f26cu);
    SYNC_STAGE(data_0209f200,0x0209f200u);
    SYNC_STAGE(data_0209f268,0x0209f268u);
    SYNC_STAGE(data_0209f24c,0x0209f24cu);
    SYNC_STAGE(data_0209f270,0x0209f270u);
    SYNC_STAGE(data_0209f2d8,0x0209f2d8u);
    SYNC_STAGE(data_0209f254,0x0209f254u);
    SYNC_STAGE(data_0209f228,0x0209f228u);
    SYNC_STAGE(data_0209f498,0x0209f498u);
    SYNC_STAGE(data_0209fc50,0x0209fc50u);
    SYNC_STAGE(data_0209fc5c,0x0209fc5cu);
    SYNC_STAGE(data_0209fc64,0x0209fc64u);
    SYNC_STAGE(data_0209f21c,0x0209f21cu);
    SYNC_STAGE(data_0209f350,0x0209f350u);
    SYNC_STAGE(data_0209f294,0x0209f294u);
    SYNC_STAGE(data_0209f2c4,0x0209f2c4u);
    SYNC_STAGE(data_0209f20c,0x0209f20cu);
    SYNC_STAGE(data_020a1166,0x020a1166u);
    SYNC_STAGE(data_0209f368,0x0209f368u);
    SYNC_STAGE(data_0209d660,0x0209d660u);
    SYNC_STAGE(data_0209d454,0x0209d454u);
    SYNC_STAGE(data_0209d45c,0x0209d45cu);
    SYNC_STAGE(data_ov002_02111180,0x02111180u);
#undef SYNC_STAGE
    // The original stage views this flag as a word. Its native boot owner
    // exposes a byte; copy the value with its proper width on each side.
    if (toPacked) {
        const uint32_t value=data_0209fc48;
        std::memcpy(addressSpace_->data(0x0209fc48u,4),&value,4);
    } else {
        uint32_t value=0;
        std::memcpy(&value,addressSpace_->data(0x0209fc48u,4),4);
        if (value>UINT8_MAX) throw std::runtime_error("Invalid stage music-control flag.");
        data_0209fc48=static_cast<uint8_t>(value);
    }
    copy(0x0209caa0u,data_0209caa0,0x32c);
}

std::vector<StageAssets::PlayerSpawnCommand> StageFlow::player_spawn_commands(
    uint8_t savedCharacter) const {
    if (!assets_)
        throw std::runtime_error("Player spawn before original stage resources are loaded.");
    std::array<uint8_t,4> characters{};
    std::copy_n(data_02092128, 4, characters.begin());
    return assets_->player_spawn_commands(
        static_cast<uint8_t>(data_0209f268), data_0208a0e0, data_0209f220,
        savedCharacter, characters, data_0209f2d8);
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
        // Use the recovered original vector scaling routine rather than
        // duplicating its DS Q12 arithmetic in the Android input bridge.
        int vector[3] = {x, y, 0};
        Vec3_MulScalarInPlace(vector, scale);
        x = vector[0];
        y = vector[1];
        magnitude = 4096;
    }
    auto &control = data_0209f498[0];
    control.nx = x;
    control.ny = y;
    control.mag = magnitude;
    control.ang = cstd::atan2(x, y);
}

int StageFlow::process_actor(uint32_t actor, const ActorProcessMethods &methods) {
    if (!addressSpace_)
        throw std::runtime_error("Actor processing requested before native stage activation.");
    packed::Space::Scope scope(*addressSpace_);
    return ActorDispatch(*addressSpace_).process(actor, methods);
}

uint32_t StageFlow::process_actor_list(uint32_t descriptor) {
    if (!addressSpace_)
        throw std::runtime_error("Actor list requested before native stage activation.");
    packed::Space::Scope scope(*addressSpace_);
    return native_fbase_process_list(descriptor);
}

int StageFlow::invoke_player_state(uint32_t player, uint32_t state,
                                   PlayerStatePhase phase) {
    if (!addressSpace_)
        throw std::runtime_error("Player callback requested before native stage activation.");
    packed::Space::Scope scope(*addressSpace_);
    return PlayerStateDispatch(*addressSpace_).invoke(player, state, phase);
}

StageRequest StageFlow::request() const {
    return {data_02092110,    data_0209f268, data_0209f1f0,
            data_02092128[0], data_0208a0e0, controller(0).mode};
}
}

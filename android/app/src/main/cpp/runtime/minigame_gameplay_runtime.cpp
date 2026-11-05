#include "minigame_gameplay_runtime.h"
#include "minigame_shared.h"
#include "stage_runtime.h"
#include "resources.h"
#include "packed_ptr.h"
#include "ntr/gx.h"
#include "ntr/mmio.h"
#include <cstring>
#include <utility>

namespace sm64ds::frontend { void bind_arm9_sources(packed::Space &); }
namespace sm64ds::stage_sources { void bind_sources(packed::Space &); }
namespace sm64ds {
MinigameGameplayRuntime::MinigameGameplayRuntime(GameHeap &heap, Cartridge &cartridge,
                                                 uint32_t scene, uint32_t parameter, const uint8_t *selectedRow)
    : space_(heap), arm9_(cartridge.arm9_data(0x020aa420u)),
      common_(cartridge.overlay_data(4)), game_(cartridge.overlay_data(6)),
      scene_(scene), parameter_(parameter) {
    map_minigame_live_arm9(space_,arm9_);
    // The selected 0x34-byte ROM row belongs to mutable menu ARM9 globals,
    // NOT immutable cartridge data. Carry the original menu's exact bytes
    // into the new overlay-6 session before constructing its scene.
    if (selectedRow)
        std::memcpy(space_.data(0x0209b308u,0x34u),selectedRow,0x34u);
    space_.map(common_.base,common_.bytes.data(),common_.bytes.size());
    // The game overlay replaces the menu overlay at the identical DS address.
    // Never map both overlay 5 and 6 into one address space.
    space_.map(game_.base,game_.bytes.data(),game_.bytes.size());
    for (const auto &span:{std::make_pair(ntr::IO_BASE,ntr::IO_SIZE),
                           std::make_pair(ntr::VRAM_BASE,ntr::VRAM_SIZE),
                           std::make_pair(ntr::PLTT_BASE,ntr::PLTT_SIZE),
                           std::make_pair(ntr::OAM_BASE,ntr::OAM_SIZE)})
        space_.map(span.first,ntr::host_ptr(span.first),span.second);
    frontend::bind_arm9_sources(space_);
    stage_sources::bind_sources(space_);
    bind_stage_runtime(space_);
    bind_minigame_shared_runtime(space_);
    validate_minigame_shared_runtime(space_);
}
uint32_t MinigameGameplayRuntime::spawn(uint8_t kind) {
    packed::Space::Scope scope(space_);
    using packed::Ptr;
    // Source path: dScene_c::SpawnIfNecessary -> scene profile -> factory.
    // Look up the actual original profile rather than a capability table.
    const uint32_t profile=*Ptr<uint32_t>(0x02090864u+scene_*4u);
    const uint32_t factory=*Ptr<uint32_t>(profile);
    *Ptr<uint16_t>(0x020a4b50u)=static_cast<uint16_t>(scene_);
    *Ptr<uint16_t>(0x020a4b54u)=static_cast<uint16_t>(scene_);
    *Ptr<uint32_t>(0x020a4b60u)=parameter_;
    *Ptr<uint8_t>(0x020a4b48u)=kind;
    // An unported original constructor fails here at its real code address.
    // Do not register a dummy factory, fake an actor or fabricate a round.
    actor_=Ptr<uint32_t()>(factory)();
    return actor_;
}
void MinigameGameplayRuntime::update() {
    packed::Space::Scope scope(space_);
    ntr::gx_begin_frame();
    // Actual recovered actor scheduler. Further missing overlay-6 component
    // callbacks will likewise surface as unlinked original code handles.
    packed::Ptr<void()>(0x02044120u)();
}
}

#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include <cstdint>
namespace sm64ds {
// Isolated original overlay-6 address space, replacing overlay 5 at its
// cartridge load address. This is an actor bootstrap, not a fake game loop.
class MinigameGameplayRuntime {
public:
    MinigameGameplayRuntime(GameHeap &, Cartridge &, uint32_t scene, uint32_t parameter,
                            const uint8_t *selectedRow = nullptr);
    uint32_t spawn(uint8_t kind);
    void update();
    uint32_t scene() const noexcept { return scene_; }
    uint32_t parameter() const noexcept { return parameter_; }
    const packed::Space &space() const noexcept { return space_; }
private:
    packed::Space space_;
    Cartridge::DataImage arm9_, common_, game_;
    uint32_t scene_, parameter_, actor_=0;
};
}

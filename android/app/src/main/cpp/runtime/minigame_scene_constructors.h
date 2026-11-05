#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include <cstdint>

namespace sm64ds {
// Resolve the retail C++ vptr through the cartridge RTTI name, without
// executing ARM instructions or transplanting a native 64-bit vtable.
uint32_t original_minigame_vptr(const Cartridge::DataImage &,const char *typeName);
// Recovered base constructor plus the actual empty component array
// constructors for the two initial 2D scenes. Does not synthesize gameplay.
uint32_t construct_minigame_2d(uint32_t scene,uint32_t originalVptr);
// Registers a native constructor at the original scene profile's factory word.
void bind_minigame_scene_factory(packed::Space &,const Cartridge::DataImage &,uint32_t scene);
}

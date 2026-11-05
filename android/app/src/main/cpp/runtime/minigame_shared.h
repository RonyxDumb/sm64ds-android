#pragma once
#include "packed_memory.h"
#include "cartridge.h"

namespace sm64ds {
// Bind only recovered original controller bodies. Graphics/game callbacks
// retain their checked original addresses and must be linked separately.
// Preserve the live ARM9 input/save aliases in both overlay-5 and overlay-6 owners.
void map_minigame_live_arm9(packed::Space &, Cartridge::DataImage &);
void bind_minigame_shared_runtime(packed::Space &);
void validate_minigame_shared_runtime(const packed::Space &);
}

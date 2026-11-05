#pragma once
#include "packed_memory.h"

namespace sm64ds {
// Bind only recovered original controller bodies. Graphics/game callbacks
// retain their checked original addresses and must be linked separately.
void bind_minigame_shared_runtime(packed::Space &);
void validate_minigame_shared_runtime(const packed::Space &);
}

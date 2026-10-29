#pragma once
#include "packed_memory.h"

namespace sm64ds {
// Native file/card and default-heap providers. Archive parsing, registration,
// mounting and unload bookkeeping remain the recovered game/SDK source.
void bind_packed_fs(packed::Space &space);
void initialize_packed_fs(packed::Space &space);
}

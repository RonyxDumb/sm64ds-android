#pragma once
#include "packed_memory.h"

namespace sm64ds {
// Cartridge-layout constructors and actor scheduler, with native code and
// checked 32-bit data handles. No native C++ vptr enters a cartridge record.
void bind_stage_runtime(packed::Space &space);
uint32_t native_stage_factory();
uint32_t native_fbase_construct(uint32_t actor);
uint32_t native_particle_tracker_construct(uint32_t tracker);
uint32_t native_simple_model_construct(uint32_t model);
uint32_t native_mesh_collider_construct(uint32_t collider);
void native_actor_post_construct(uint32_t actor);
}

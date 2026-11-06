#pragma once
#include "packed_memory.h"

namespace sm64ds {
// Cartridge-layout constructors and actor scheduler, with native code and
// checked 32-bit data handles. No native C++ vptr enters a cartridge record.
void bind_stage_runtime(packed::Space &space);
void bind_stage_audio_runtime(packed::Space &space);
void bind_stage_arm9_math(packed::Space &space);
void bind_stage_overlay_runtime(packed::Space &space,unsigned id);
uint32_t native_stage_factory();
uint32_t native_fbase_construct(uint32_t actor);
uint32_t native_particle_tracker_construct(uint32_t tracker);
uint32_t native_simple_model_construct(uint32_t model);
uint32_t native_mesh_collider_construct(uint32_t collider);
void native_actor_post_construct(uint32_t actor);
// dScene_c::BeforeBehavior and func_020431c4 over the native fader/scene request.
int scene_before_behavior(uint32_t actor);
int scene_waiting_children(uint32_t actor);
int actor_before_behavior(uint32_t actor);
int actor_before_render(uint32_t actor);
void fbase_after_process(uint32_t actor,uint32_t success);
}

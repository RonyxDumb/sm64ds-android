#pragma once
#include "packed_memory.h"
#include <cstdint>

namespace sm64ds {
// ARM EABI member pointers as recovered from fBase_c::Process:
// { function address OR virtual-slot byte offset, signed this adjustment
// with low bit denoting virtual dispatch }. These are cartridge words,
// never native C++ pointer-to-member representations.
struct ActorProcessMethods {
    packed::MemberCall action;
    packed::MemberCall before;
    packed::MemberCall after;
};
static_assert(sizeof(ActorProcessMethods)==24);

// Linkable transcriptions of the five recovered fBase_c defaults. Address
// ownership comes from config/arm9/symbols.txt, with DS field offsets from
// include/fBase_c.h. They consume checked packed 32-bit actor handles.
int native_fbase_init(uint32_t actor);
int native_fbase_behavior(uint32_t actor);
int native_fbase_render(uint32_t actor);
bool native_fbase_before_behavior(uint32_t actor);
bool native_fbase_before_render(uint32_t actor);
// Original func_02043fdc: process the intrusive behavior/render list through
// ARM EABI member callbacks, preserving next-node-before-dispatch semantics.
uint32_t native_fbase_process_list(uint32_t descriptor);

// Source: src/runtime/actor/d_actor.cpp. These use the exact DS actor
// fields at offsets 0x05c..0x0c0 but native 64-bit C++ code; no DS vptr casts.
void native_actor_set_ranges(uint32_t actor, int32_t y, int32_t radius,
                             int32_t distance, int32_t farDistance);
void native_actor_clamp_pos(uint32_t actor);
void native_actor_update_horz_speed(uint32_t actor);
void native_actor_update_pos_only_speed(uint32_t actor, uint32_t collision);
void native_actor_update_pos(uint32_t actor, uint32_t collision);

class ActorDispatch {
public:
    explicit ActorDispatch(packed::Space &space) : space_(space) {}
    // Mirrors original src/runtime/actor/ActorBase.cpp::fBase_c::Process.
    // All three callbacks MUST be registered recovered native functions.
    // No guest ARM instructions, fake gameplay or silent no-op callbacks.
    int process(uint32_t object, const ActorProcessMethods &methods) const;
    // fLiNdBaPr_c: next at +4 and owner at +8. ProcessListDescriptor:
    // {head, tail, {member pointer, this-adjustment}} = 16 bytes.
    // currentNodeCell is the original data_020a4b68 global handle.
    uint32_t process_list(uint32_t descriptor, uint32_t currentNodeCell) const;
private:
    packed::Space &space_;
};
// The recovered Player::State contains three ARM EABI member function
// pointers at offsets +0, +8, +16. Player::Behavior invokes Main each tick;
// Player::ChangeState tests Cleanup first and invokes Init after transition.
struct PlayerStateMethods {
    packed::MemberCall init;
    packed::MemberCall main;
    packed::MemberCall cleanup;
};
static_assert(sizeof(PlayerStateMethods)==0x18);
enum class PlayerStatePhase { Init, Main, Cleanup };
class PlayerStateDispatch {
public:
    explicit PlayerStateDispatch(packed::Space &space) : space_(space) {}
    // Dispatch ONLY the recovered member callback. State changes, physics and
    // animation remain owned by the yet-to-be-connected original Player.
    int invoke(uint32_t player, uint32_t state, PlayerStatePhase phase) const;
private:
    packed::Space &space_;
};
} // namespace sm64ds

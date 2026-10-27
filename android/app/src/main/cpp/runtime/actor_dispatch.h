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

class ActorDispatch {
public:
    explicit ActorDispatch(packed::Space &space) : space_(space) {}
    // Mirrors original src/runtime/actor/ActorBase.cpp::fBase_c::Process.
    // All three callbacks MUST be registered recovered native functions.
    // No guest ARM instructions, fake gameplay or silent no-op callbacks.
    int process(uint32_t object, const ActorProcessMethods &methods) const;
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

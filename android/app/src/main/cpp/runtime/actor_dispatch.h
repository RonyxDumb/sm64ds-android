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
} // namespace sm64ds

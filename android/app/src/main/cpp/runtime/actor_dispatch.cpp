#include "actor_dispatch.h"
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace sm64ds {
namespace {
template<class Function>
Function native_callback(uintptr_t code) {
    Function callback = nullptr;
    static_assert(sizeof callback == sizeof code);
    std::memcpy(&callback, &code, sizeof callback);
    return callback;
}
}
// Recovered fBase_c::InitResources, Behavior and Render default to
// successful completion; their real subclass overrides must still be
// linked individually. No success fallback is installed for unknown methods.
int native_fbase_init(uint32_t actor) {
    (void)packed::Space::current().data(actor, 0x50);
    return 1;
}
int native_fbase_behavior(uint32_t actor) {
    (void)packed::Space::current().data(actor, 0x50);
    return 1;
}
int native_fbase_render(uint32_t actor) {
    (void)packed::Space::current().data(actor, 0x50);
    return 1;
}
int native_fbase_before_behavior(uint32_t actor) {
    // fBase_c::shouldBeKilled at +0x0f, pauseFlags at +0x13.
    const auto *state = static_cast<const uint8_t *>(
        packed::Space::current().data(actor, 0x50));
    return state[0x0f] || (state[0x13] & 2u) ? 0 : 1;
}
int native_fbase_before_render(uint32_t actor) {
    const auto *state = static_cast<const uint8_t *>(
        packed::Space::current().data(actor, 0x50));
    return state[0x0f] || (state[0x13] & 8u) ? 0 : 1;
}
// src/runtime/actor/ActorBase.cpp::func_02043fdc walks each 16-byte
// process-list node and saves the next link before invoking the member
// callback. The DS process-list header is {head,tail,pmf} (0x10 bytes).
// The callback has real native ARM64 code only after Space::bind().
uint32_t ActorDispatch::process_list(uint32_t descriptor, uint32_t currentNodeCell) const {
    const auto word = [this](uint32_t at) {
        uint32_t value = 0;
        std::memcpy(&value, space_.data(at, sizeof value), sizeof value);
        return value;
    };
    const auto write = [this](uint32_t at, uint32_t value) {
        std::memcpy(space_.data(at, sizeof value), &value, sizeof value);
    };
    // Validate both the list descriptor and the current-list global before
    // invoking anything; a malformed 32-bit ROM address is never dereferenced.
    (void)space_.data(descriptor,16);
    (void)space_.data(currentNodeCell,4);
    packed::MemberCall callback{};
    std::memcpy(&callback,space_.data(descriptor+8,sizeof callback),sizeof callback);
    if (!callback.entry && !callback.adjustment) return 1;
    uint32_t node = word(descriptor);
    std::unordered_set<uint32_t> visited;
    while (node) {
        if (!visited.insert(node).second || visited.size()>32768u)
            throw std::runtime_error("Cyclic or oversized native actor process list.");
        // A recovered fLiNdBaPr_c is 0x10 bytes, not an ARM64 object.
        (void)space_.data(node,0x10);
        const uint32_t next = word(node+4);
        const uint32_t owner = word(node+8);
        if (!owner)
            throw std::runtime_error("Actor list contains an ownerless node.");
        write(currentNodeCell,node);
        const auto target = space_.resolve_member(owner,callback);
        using Action = int (*)(uint32_t);
        (void)native_callback<Action>(target.code)(target.object);
        node = next;
    }
    write(currentNodeCell,0);
    return 1;
}
uint32_t native_fbase_process_list(uint32_t descriptor) {
    auto &space = packed::Space::current();
    return ActorDispatch(space).process_list(descriptor,0x020a4b68u);
}

// Source: fBase_c::Process in src/runtime/actor/ActorBase.cpp.
// Callback signatures are the source's action int(), guard bool() and
// after hook void(u32). The first native argument is a checked packed
// actor handle, resolved by Space::resolve_member for adjusted 'this'.
int ActorDispatch::process(uint32_t object, const ActorProcessMethods &methods) const {
    const auto guard = space_.resolve_member(object, methods.before);
    using Guard = bool (*)(uint32_t);
    if (!native_callback<Guard>(guard.code)(guard.object)) {
        const auto after = space_.resolve_member(object, methods.after);
        using After = void (*)(uint32_t,uint32_t);
        native_callback<After>(after.code)(after.object,0u);
        return 0;
    }
    const auto action = space_.resolve_member(object, methods.action);
    using Action = int (*)(uint32_t);
    const int result = native_callback<Action>(action.code)(action.object);
    const uint32_t status = result == -1 ? 3u : result == 1 ? 2u : 1u;
    const auto after = space_.resolve_member(object, methods.after);
    using After = void (*)(uint32_t,uint32_t);
    native_callback<After>(after.code)(after.object,status);
    return result;
}
int PlayerStateDispatch::invoke(uint32_t player, uint32_t state,
                                PlayerStatePhase phase) const {
    PlayerStateMethods record{};
    // Overlay records stay three 8-byte ARM member pointers, even on ARM64.
    // Never reinterpret a ROM State as a native C++ object or function table.
    std::memcpy(&record, space_.data(state, sizeof record), sizeof record);
    const auto &member = phase == PlayerStatePhase::Init ? record.init
                        : phase == PlayerStatePhase::Main ? record.main
                                                         : record.cleanup;
    // Player::ChangeState and Behavior test the first word, not the pair.
    if (!member.entry)
        return phase == PlayerStatePhase::Main ? 0 : 1;
    const auto target = space_.resolve_member(player, member);
    using Callback = int (*)(uint32_t);
    return native_callback<Callback>(target.code)(target.object);
}
} // namespace sm64ds

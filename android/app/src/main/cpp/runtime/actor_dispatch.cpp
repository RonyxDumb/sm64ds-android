#include "actor_dispatch.h"
#include <cstring>

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

#include "actor_dispatch.h"
#include <algorithm>
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
        if (!visited.insert(node).second)
            throw std::runtime_error("Cyclic native actor process list.");
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

// Recovered dActor_c math and collision pushback, projected onto packed
// records at the original ROM field offsets (include/dActor_c.h and include/dCc_c.h).
// No unported native C++ vtable/constructor is fabricated.
namespace {
int32_t load_actor32(const packed::Space &space, uint32_t base, unsigned offset) {
    int32_t v;
    std::memcpy(&v,space.data(base+offset,sizeof v),sizeof v);
    return v;
}
void store_actor32(const packed::Space &space, uint32_t base, unsigned offset, int32_t v) {
    std::memcpy(space.data(base+offset,sizeof v),&v,sizeof v);
}
int32_t add32(int32_t a, int32_t b) {
    const uint32_t sum=static_cast<uint32_t>(a)+static_cast<uint32_t>(b);
    int32_t v;
    std::memcpy(&v,&sum,sizeof v);
    return v;
}
int32_t fixed_mul(int32_t a, int32_t b) {
    return static_cast<int32_t>((static_cast<int64_t>(a)*b+0x800ll)>>12);
}
}
void native_actor_set_ranges(uint32_t actor, int32_t y, int32_t radius,
                             int32_t distance, int32_t farDistance) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0xd0);
    store_actor32(space,actor,0xb4,y);
    store_actor32(space,actor,0xb8,radius>>3);
    store_actor32(space,actor,0xbc,distance>>3);
    store_actor32(space,actor,0xc0,farDistance>>3);
}
void native_actor_clamp_pos(uint32_t actor) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0xd0);
    for (unsigned offset : {0x5cu,0x60u,0x64u}) {
        const int32_t position=load_actor32(space,actor,offset);
        store_actor32(space,actor,offset,
            std::clamp(position,-0x7d00000,0x7d00000));
    }
}
void native_actor_update_horz_speed(uint32_t actor) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0xd0);
    const int32_t horz=load_actor32(space,actor,0x98);
    const int32_t vert=load_actor32(space,actor,0xa8);
    const int32_t accel=load_actor32(space,actor,0x9c);
    const int32_t terminal=load_actor32(space,actor,0xa0);
    const int32_t newVert=std::max(terminal,add32(vert,accel));
    int32_t speedX=0,speedZ=0;
    if(horz) {
        uint16_t angle=0;
        std::memcpy(&angle,space.data(actor+0x94,2),2);
        const uint32_t idx=(angle>>4)*2u;
        int16_t trig[2]{};
        std::memcpy(trig,space.data(0x02082214u+idx*2u,4),4);
        speedX=fixed_mul(horz,trig[0]);
        speedZ=fixed_mul(horz,trig[1]);
    }
    store_actor32(space,actor,0xa4,speedX);
    store_actor32(space,actor,0xa8,newVert);
    store_actor32(space,actor,0xac,speedZ);
}
void native_actor_update_pos_only_speed(uint32_t actor, uint32_t collision) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0xd0);
    const int32_t px=load_actor32(space,actor,0x5c);
    const int32_t py=load_actor32(space,actor,0x60);
    const int32_t pz=load_actor32(space,actor,0x64);
    int32_t x=add32(px,load_actor32(space,actor,0xa4));
    const int32_t y=add32(py,load_actor32(space,actor,0xa8));
    int32_t z=add32(pz,load_actor32(space,actor,0xac));
    if(collision) {
        (void)space.data(collision,0x30);
        x=add32(x,load_actor32(space,collision,0x0c));
        z=add32(z,load_actor32(space,collision,0x14));
    }
    store_actor32(space,actor,0x5c,x);
    store_actor32(space,actor,0x60,y);
    store_actor32(space,actor,0x64,z);
    native_actor_clamp_pos(actor);
}
void native_actor_update_pos(uint32_t actor, uint32_t collision) {
    native_actor_update_horz_speed(actor);
    native_actor_update_pos_only_speed(actor,collision);
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

#include "collision_dispatch.h"
#include "packed_memory.h"
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace sm64ds {
namespace {
// fLiNdBa_c-style collision pointers are encoded DS words at +0x28/+0x2c.
// BSS data_0209cee8 is the source's global collision-list head.
constexpr uint32_t kCollisionHead = 0x0209cee8u;
uint32_t get(const packed::Space &space, uint32_t object, uint32_t offset) {
    uint32_t value=0;
    std::memcpy(&value,space.data(object+offset,4),4);
    return value;
}
void put(const packed::Space &space, uint32_t object, uint32_t offset,
         uint32_t value) {
    std::memcpy(space.data(object+offset,4),&value,4);
}
void validate(const packed::Space &space, uint32_t object) {
    (void)space.data(object,0x30);
}
}
void native_collision_init(uint32_t collider, int32_t radius, int32_t height,
                           uint32_t flags, uint32_t vulnerableFlags) {
    auto &space=packed::Space::current();
    validate(space,collider);
    put(space,collider,0x04,static_cast<uint32_t>(radius));
    put(space,collider,0x08,static_cast<uint32_t>(height));
    put(space,collider,0x18,flags);
    put(space,collider,0x1c,vulnerableFlags);
}
void native_collision_clear(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    for(uint32_t off : {0x0cu,0x10u,0x14u,0x20u,0x24u})
        put(space,collider,off,0);
}
void native_collision_update(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    if(get(space,collider,0x18)&1u) return;
    const uint32_t previousHead=get(space,kCollisionHead,0);
    // The caller must first unlink a collider that is already active.
    // Otherwise Nintendo's Update would create a self-cycle.
    if(previousHead==collider ||
       get(space,collider,0x28)!=0 || get(space,collider,0x2c)!=0)
        throw std::runtime_error("Collision Update on an already-linked node.");
    if(previousHead) {
        validate(space,previousHead);
        put(space,previousHead,0x28,collider);
    }
    put(space,collider,0x2c,previousHead);
    put(space,kCollisionHead,0,collider);
}
void native_collision_unlink(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    const uint32_t previous=get(space,collider,0x28);
    const uint32_t next=get(space,collider,0x2c);
    if(previous) {
        validate(space,previous);
        put(space,previous,0x2c,next);
    } else if(get(space,kCollisionHead,0)==collider) {
        put(space,kCollisionHead,0,next);
    }
    if(next) {
        validate(space,next);
        put(space,next,0x28,previous);
    }
    put(space,collider,0x28,0);
    put(space,collider,0x2c,0);
}
} // namespace sm64ds

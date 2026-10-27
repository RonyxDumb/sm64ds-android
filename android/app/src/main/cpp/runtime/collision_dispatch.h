#pragma once
#include <cstdint>
namespace sm64ds {
// Original dCc_c recovered functions, using DS's 0x30-byte layout in the
// checked packed address space, not a widened native polymorphic object.
void native_collision_init(uint32_t collider, int32_t radius, int32_t height,
                           uint32_t flags, uint32_t vulnerableFlags);
void native_collision_clear(uint32_t collider);
void native_collision_update(uint32_t collider);
void native_collision_unlink(uint32_t collider);
} // namespace sm64ds

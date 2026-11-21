/* Force-included in every decomp translation unit of the native port.
 * Maps CodeWarrior intrinsics and extensions onto clang/GCC equivalents. */
#pragma once
#define PLATFORM_PORT 1
#define SM64DS_NATIVE_PORT 1
#include "port_hw.h"

#define __memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define __builtin__clear(p, n) __builtin_memset((p), 0, (n))
#define __clear(p, n) __builtin_memset((p), 0, (n))

/* mwcc aligns 8-byte members (long long, double) to 4 bytes; 32-bit ARM and AArch64 ILP32 align them to 8. A typedef
 * can lower the alignment: tools/portify.py declares such members with these types, so that the game's structures
 * keep the DS layout (their layout assertions check it). */
typedef long long port_ll4 __attribute__((aligned(4)));
typedef unsigned long long port_ull4 __attribute__((aligned(4)));
typedef double port_double4 __attribute__((aligned(4)));

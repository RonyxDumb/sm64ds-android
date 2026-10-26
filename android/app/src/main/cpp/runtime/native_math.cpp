#include "types.h"
#include <climits>
#include <stdexcept>

namespace cstd {
// Native counterparts of the two Nitro arithmetic units. Preserve integer
// rounding; using libm atan2/sqrt here would change the game's input thresholds.
Fix12i fdiv(Fix12i numerator, Fix12i denominator) {
    if (!denominator)
        throw std::runtime_error("Fixed-point divide by zero.");
    const s64 scaled = s64(numerator) * 0x100000000LL;
    const s64 quotient =
        scaled == LLONG_MIN && denominator == -1 ? LLONG_MIN : scaled / denominator;
    return static_cast<s32>((quotient + 0x80000) >> 20);
}

int sqrt(unsigned long long value) {
    // cstd_sqrt.cpp submits value << 2 to the unsigned 64-bit unit, then
    // returns (integer_root + 1) >> 1. The shift intentionally wraps at 64 bits.
    u64 remaining = value << 2;
    u64 root = 0;
    u64 bit = 1ULL << 62;
    while (bit > remaining)
        bit >>= 2;
    while (bit) {
        if (remaining >= root + bit) {
            remaining -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<s32>(static_cast<u32>(root) + 1u) >> 1;
}
}

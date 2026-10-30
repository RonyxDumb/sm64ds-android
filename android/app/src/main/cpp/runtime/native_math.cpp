#include "types.h"
#include "ntr/mmio.h"

namespace cstd {
// Synchronous SDK calls share their register state with async game callers.
long long ldiv(int numerator, int denominator) {
    ntr::io_write(0x04000280,1,2);
    ntr::io_write(0x04000290,u64(u32(numerator))<<32,8);
    ntr::io_write(0x04000298,u32(denominator),8);
    return static_cast<s64>(ntr::io_read(0x040002a0,8));
}

Fix12i fdiv(Fix12i numerator, Fix12i denominator) {
    const s64 quotient=ldiv(numerator,denominator);
    const s64 rounded=static_cast<s64>(static_cast<u64>(quotient)+0x80000);
    return static_cast<s32>(rounded >> 20);
}

int div(int numerator, int denominator) {
    ntr::io_write(0x04000280,0,2);
    ntr::io_write(0x04000290,u32(numerator),4);
    ntr::io_write(0x04000298,u32(denominator),8);
    return static_cast<s32>(ntr::io_read(0x040002a0,4));
}
int mod(int numerator, int denominator) {
    div(numerator,denominator);
    return static_cast<s32>(ntr::io_read(0x040002a8,4));
}

int sqrt(unsigned long long value) {
    const auto ime=ntr::io_read(0x04000208,2);
    ntr::io_write(0x04000208,0,2);
    ntr::io_write(0x040002b0,1,2);
    ntr::io_write(0x040002b8,value<<2,8);
    ntr::io_write(0x04000208,ime,2);
    const auto root=static_cast<u32>(ntr::io_read(0x040002b4,4));
    return static_cast<s32>(root+1u)>>1;
}
}

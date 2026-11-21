// I/O registers with side effects (tools/portify.py turns the game's writes to them into PORT_IO_WRITE), and the
// divider and square root units, whose result registers are computed when read.
#include "port.h"

#include "types.h"
#include "hostapi.h"

void port_io_write(volatile void *reg, unsigned size, unsigned value) {
    uint32_t addr = (uint32_t) (uintptr_t) reg;
    if (addr == 0x04000400) {
        gx3d_fifo_write(value);
        return;
    }
    if (addr >= 0x04000440 && addr < 0x04000600) {
        gx3d_port_write(addr, value);
        return;
    }
    if (addr == 0x04000214) { // IF: writing 1 acknowledges
        IO32(0x214) &= ~value;
        return;
    }
    switch (size) {
    case 1: *(volatile uint8_t *) reg = (uint8_t) value; break;
    case 2: *(volatile uint16_t *) reg = (uint16_t) value; break;
    default: *(volatile uint32_t *) reg = value; break;
    }
    if (addr >= 0x04000240 && addr < 0x0400024a) {
        static const int8_t bank_of[10] = {VRAM_A, VRAM_B, VRAM_C, VRAM_D, VRAM_E, VRAM_F, VRAM_G, -1, VRAM_H, VRAM_I};
        for (unsigned i = 0; i < size; i++) {
            uint32_t off = addr - 0x04000240 + i;
            if (off < 10 && bank_of[off] >= 0) {
                hapi_vram_remap(bank_of[off]);
            }
        }
    }
}

unsigned char *port_cp_div(void) {
    uint16_t cnt = IO16(0x280);
    int64_t numer = *(volatile int64_t *) (uintptr_t) 0x04000290;
    int64_t denom = *(volatile int64_t *) (uintptr_t) 0x04000298;
    int64_t q, r;
    switch (cnt & 3) {
    case 0: // 32 / 32
        numer = (int32_t) numer;
        denom = (int32_t) denom;
        break;
    case 1: // 64 / 32
    case 3:
        denom = (int32_t) denom;
        break;
    default: // 64 / 64
        break;
    }
    if (*(volatile int64_t *) (uintptr_t) 0x04000298 == 0) {
        IO16(0x280) = cnt | 0x4000;
    } else {
        IO16(0x280) = cnt & ~0x4000;
    }
    if (denom == 0) {
        q = numer < 0 ? 1 : -1;
        r = numer;
        if ((cnt & 3) == 0) {
            q ^= (int64_t) 0xffffffff00000000ull; // the upper word comes out inverted in 32-bit mode
        }
    } else if (numer == INT64_MIN && denom == -1) {
        q = INT64_MIN;
        r = 0;
    } else {
        q = numer / denom;
        r = numer % denom;
    }
    if ((cnt & 3) == 0 && numer == INT32_MIN && denom == -1) {
        q = (int64_t) 0x80000000u; // 32-bit overflow: 0x80000000 with a zero upper word
    }
    *(volatile int64_t *) (uintptr_t) 0x040002a0 = q;
    *(volatile int64_t *) (uintptr_t) 0x040002a8 = r;
    return (unsigned char *) (uintptr_t) 0x040002a0;
}

static uint32_t isqrt64(uint64_t v) {
    uint64_t res = 0, bit = 1ull << 62;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) res;
}

unsigned char *port_cp_sqrt(void) {
    uint64_t param = *(volatile uint64_t *) (uintptr_t) 0x040002b8;
    if (!(IO16(0x2b0) & 1)) {
        param = (uint32_t) param;
    }
    IO32(0x2b4) = isqrt64(param);
    return (unsigned char *) (uintptr_t) 0x040002b4;
}

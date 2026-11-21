// The compiler runtime and C library pieces Super Mario 64 DS's code calls by name: mwcc's soft-float helpers, the
// C++ array construction/destruction helpers (MSL C++ runtime), and assembly primitives of the decomp.
#include "port.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void (*ctor_t)(void *);
typedef void (*dtor_t)(void *);

// ------------------------------------------------------------------------------------------------ C++ runtime
void __cxa_vec_ctor(void *array, unsigned int count, unsigned int size, ctor_t ctor, dtor_t dtor) {
    (void) dtor;
    if (!ctor) return;
    for (unsigned int i = 0; i < count; i++) ctor((char *) array + i * size);
}

void __cxa_vec_cleanup(void *array, unsigned int count, unsigned int size, dtor_t dtor) {
    if (!dtor) return;
    for (unsigned int i = count; i-- > 0;) dtor((char *) array + i * size);
}

// The partial-array destroy the construct helpers' landing pads call (src/unnamed/arm9/0207/func_02073300.cpp).
void func_02073300(void *block, unsigned int n, unsigned int size, dtor_t dtor) { __cxa_vec_cleanup(block, n, size, dtor); }

void __cxa_pure_virtual(void) { port_fatal("pure virtual function called"); }
void port_std_terminate(void) __asm__("_ZSt9terminatev");
void port_std_terminate(void) { port_fatal("std::terminate"); }
void port_builtin_trap(void) __asm__("_ZN4cstd14__builtin_trapEv");
void port_builtin_trap(void) { port_fatal("cstd::__builtin_trap"); }
// Exception unwinding (the game has exceptions off but links the MSL handler): never reached.
void func_02071790(void *context, void *iterator, int pad) {
    (void) context, (void) iterator, (void) pad;
    port_fatal("exception unwinding");
}
void sys_exit(void) { port_fatal("sys_exit"); }

// ------------------------------------------------------------------------------------------------ mwcc soft float
static inline float f_of(int32_t bits) { float f; memcpy(&f, &bits, 4); return f; }
static inline int32_t bits_of(float f) { int32_t b; memcpy(&b, &f, 4); return b; }

double func_01ff8708(double x, double y) { return x * y; }                     // _dmul
int func_01ffa344(int a, int b) { (void) b; return (int) f_of(a); }             // _ffix (float bits -> int)
int32_t func_01ffa4bc(int32_t a) { return bits_of((float) a); }                 // _fflt (int -> float bits)
int func_01ff98f4(int32_t a, int32_t b) { return f_of(a) < f_of(b); }           // float a < b
int func_01ff99a4(int32_t a, int32_t b) { return f_of(a) > f_of(b); }           // float a > b
int func_01ff9e2c(double a, double b) { return a < b; }                         // double a < b
int func_01ffaa34(int64_t v, int64_t w) { return (int) (v / w); }               // _ll_sdiv
#ifndef __arm__
int __aeabi_idiv(int a, int b) { return a / b; }
#endif

// ------------------------------------------------------------------------------------------------ assembly primitives
// swp: atomic exchange (one CPU: a plain exchange).
uint32_t func_0206dab4(uint32_t value, void *lock) {
    uint32_t old = *(volatile uint32_t *) lock;
    *(volatile uint32_t *) lock = value;
    return old;
}
// IRQ::SetFIQState(false) / IRQ::SetIRQState(false) entries: the previous enable state.
int func_0206d9cc(void) { return 0; }
int func_0206da18(void) { return 0; }

// The card pulled-out callback: a phone's card is never pulled out.
void func_02061128(int (*callback)(void)) { (void) callback; }
// A wireless request (the game's MP setup): the wireless is off.
int func_0206259c(void *callback, uint32_t param) { (void) callback, (void) param; return 12; }

// A complete-object destructor of a type whose destructor does nothing (tools/gapgen.py aliases such wrappers here).
void *port_trivial_dtor(void *self) { return self; }

void *__cxa_vec_new(unsigned int count, unsigned int size, unsigned int padding, ctor_t ctor, dtor_t dtor) {
    char *block = calloc(1, count * size + padding);
    if (!block) return NULL;
    char *array = block + padding;
    if (padding) ((unsigned int *) array)[-1] = count;
    __cxa_vec_ctor(array, count, size, ctor, dtor);
    return array;
}
// The cleanup the construct helpers' landing pads call (src/unnamed/arm9/0207/func_02073534.cpp): no exceptions here.
void func_02073534(void *base, void *current, unsigned int size, dtor_t dtor) {
    for (char *p = current; p > (char *) base;) {
        p -= size;
        dtor(p);
    }
}
void port_std_dthandler(void) __asm__("_ZSt9dthandlerv");
void port_std_dthandler(void) { port_fatal("std::dthandler"); }

// MSL console I/O (semihosting on the DS): to the log.
int __write_console(long handle, unsigned char *buffer, size_t *count, void *idle) {
    (void) handle, (void) idle;
    char line[256];
    size_t n = *count < sizeof(line) - 1 ? *count : sizeof(line) - 1;
    memcpy(line, buffer, n);
    line[n] = 0;
    port_log("console: %s", line);
    return 0;
}
int __read_console(long handle, unsigned char *buffer, size_t *count, void *idle) {
    (void) handle, (void) buffer, (void) idle;
    *count = 0;
    return 0;
}
int __close_console(long handle) { (void) handle; return 0; }
// The "C" locale's wide/multibyte conversions (MSL): one byte per character.
int __mbtowc_noconv(unsigned short *pwc, const char *s, size_t n) {
    if (!s) return 0;
    if (!n) return -1;
    if (pwc) *pwc = (unsigned char) *s;
    return *s ? 1 : 0;
}
int __wctomb_noconv(char *s, unsigned short wc) {
    if (!s) return 0;
    *s = (char) wc;
    return 1;
}

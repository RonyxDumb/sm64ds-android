// Host side of the arm64 build: loads the guest image (the game and the DS platform layer compiled with 32-bit
// pointers, tools/build_guest.py) below 4 GiB and connects it to the host.
//
// - The image is an ELF32 AArch64 executable linked at GUEST_BASE, embedded in this library; its segments are
//   copied to their addresses.
// - Everything the guest can see must be below 4 GiB: its image, the DS memory map, the memory the host gives it
//   (host_low_alloc) and the stacks it runs on. Threads the host creates for the guest get stacks there; host threads
//   that call into the guest (renderer, audio) switch to a stack there for the call (guest_call).
// - The guest calls the host through an import table of 64-bit function addresses (HOST_IMPORTS).
#include "port.h"
#include "hostapi.h"
#include "guest_if.h"
#include "guest_abi.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

extern const uint8_t guest_image_start[], guest_image_end[]; // guest_image.S

// ------------------------------------------------------------------------------------------------ low memory
#define LOW_ARENA_BASE 0x40000000u
#define LOW_ARENA_SIZE (192u << 20)
#define STACK_BASE 0x50000000u
#define STACK_SIZE (2u << 20)
#define STACK_COUNT 64

static uint8_t *s_arena;
static size_t s_arena_used;
static pthread_mutex_t s_arena_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *s_stacks;
static uint64_t s_stack_used; // bit per stack

void *host_low_alloc(size_t size) {
    pthread_mutex_lock(&s_arena_lock);
    size = (size + 63) & ~(size_t) 63;
    void *p = NULL;
    if (s_arena && s_arena_used + size <= LOW_ARENA_SIZE) {
        p = s_arena + s_arena_used;
        s_arena_used += size;
    }
    pthread_mutex_unlock(&s_arena_lock);
    if (!p) port_log("bridge: low memory exhausted (%zu bytes)", size);
    return p;
}

static uint8_t *alloc_stack(void) {
    pthread_mutex_lock(&s_arena_lock);
    for (int i = 0; i < STACK_COUNT; i++) {
        if (!(s_stack_used & (1ull << i))) {
            s_stack_used |= 1ull << i;
            pthread_mutex_unlock(&s_arena_lock);
            return s_stacks + (size_t) i * STACK_SIZE;
        }
    }
    pthread_mutex_unlock(&s_arena_lock);
    port_fatal("bridge: out of guest stacks");
}

static void free_stack(uint8_t *stack) {
    pthread_mutex_lock(&s_arena_lock);
    s_stack_used &= ~(1ull << ((stack - s_stacks) / STACK_SIZE));
    pthread_mutex_unlock(&s_arena_lock);
}

static void *map_low(uint32_t addr, size_t size) {
    void *p = mmap((void *) (uintptr_t) addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p != (void *) (uintptr_t) addr) {
        port_log("bridge: cannot map %08x+%zx", addr, size);
        if (p != MAP_FAILED) munmap(p, size);
        return NULL;
    }
    return p;
}

// ------------------------------------------------------------------------------------------------ calls
// Calls fn(a0, a1, a2) with the stack pointer at stack_top (guest_call.S).
uint64_t guest_call_on_stack(uint64_t fn, uint64_t a0, uint64_t a1, uint64_t a2, void *stack_top);
__asm__(".text\n.globl guest_call_on_stack\n.type guest_call_on_stack, %function\n"
        "guest_call_on_stack:\n"
        "  stp x29, x30, [sp, #-32]!\n"
        "  str x19, [sp, #16]\n"
        "  mov x29, sp\n"
        "  mov x19, sp\n"
        "  mov x16, x0\n"
        "  mov x0, x1\n"
        "  mov x1, x2\n"
        "  mov x2, x3\n"
        "  mov sp, x4\n"
        "  blr x16\n"
        "  mov sp, x19\n"
        "  ldr x19, [sp, #16]\n"
        "  ldp x29, x30, [sp], #32\n"
        "  ret\n");

static __thread uint8_t *t_call_stack; // this host thread's stack for calls into the guest
static __thread bool t_on_guest_stack;  // threads started for the guest already run on one

static uint64_t guest_call(uint32_t fn, uint64_t a0, uint64_t a1, uint64_t a2) {
    if (t_on_guest_stack) {
        return ((uint64_t (*)(uint64_t, uint64_t, uint64_t)) (uintptr_t) fn)(a0, a1, a2);
    }
    if (!t_call_stack) t_call_stack = alloc_stack();
    return guest_call_on_stack(fn, a0, a1, a2, t_call_stack + STACK_SIZE);
}

typedef struct Start {
    void (*fn)(void *);
    void *arg;
    uint8_t *stack;
} Start;

static void *thread_main(void *p) {
    Start *s = p;
    t_on_guest_stack = true;
    s->fn(s->arg);
    return NULL;
}

void host_start_guest_thread(void (*fn)(void *), void *arg) {
    Start *s = malloc(sizeof(*s));
    s->fn = fn;
    s->arg = arg;
    s->stack = alloc_stack();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, s->stack, STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    if (pthread_create(&t, &attr, thread_main, s) != 0) {
        port_fatal("bridge: pthread_create failed");
    }
    pthread_attr_destroy(&attr);
    (void) free_stack;
}

// ------------------------------------------------------------------------------------------------ loading
typedef struct Elf32Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Elf32Header;
typedef struct Elf32Phdr {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} Elf32Phdr;

static const GuestHeader *s_header;

bool guest_load(void) {
    s_arena = map_low(LOW_ARENA_BASE, LOW_ARENA_SIZE);
    s_stacks = map_low(STACK_BASE, (size_t) STACK_SIZE * STACK_COUNT);
    if (!s_arena || !s_stacks) return false;
    const uint8_t *img = guest_image_start;
    const Elf32Header *eh = (const Elf32Header *) img;
    if (memcmp(eh->ident, "\x7f" "ELF", 4) != 0 || eh->ident[4] != 1 || eh->machine != 183) {
        port_log("bridge: the embedded guest is not an ELF32 AArch64 image");
        return false;
    }
    for (int i = 0; i < eh->phnum; i++) {
        const Elf32Phdr *ph = (const Elf32Phdr *) (img + eh->phoff + i * eh->phentsize);
        if (ph->type != 1) continue; // PT_LOAD
        uint32_t start = ph->vaddr & ~0xffffu, end = (ph->vaddr + ph->memsz + 0xffff) & ~0xffffu;
        uint8_t *p = map_low(start, end - start);
        if (!p) return false;
        memcpy((void *) (uintptr_t) ph->vaddr, img + ph->offset, ph->filesz);
        int prot = PROT_READ | (ph->flags & 2 ? PROT_WRITE : 0) | (ph->flags & 1 ? PROT_EXEC : 0);
        if (mprotect(p, end - start, prot) != 0) {
            port_log("bridge: mprotect of the guest image failed");
            return false;
        }
        if (ph->flags & 1) __builtin___clear_cache((char *) p, (char *) p + (end - start));
    }
    s_header = (const GuestHeader *) (uintptr_t) GUEST_BASE;
    if (s_header->magic != GUEST_MAGIC) {
        port_log("bridge: no guest header");
        return false;
    }
    uint64_t *imports = (uint64_t *) (uintptr_t) s_header->imports;
    int n = 0;
#define FILL(name) imports[n++] = (uint64_t) (uintptr_t) name;
    HOST_IMPORTS(FILL)
    if ((uint32_t) n != s_header->import_count) {
        port_log("bridge: import count mismatch (%d, guest %u)", n, s_header->import_count);
        return false;
    }
    port_log("bridge: guest image loaded (%zu KiB)", (size_t) (guest_image_end - guest_image_start) / 1024);
    return true;
}

// ------------------------------------------------------------------------------------------------ interface
static void start_thread(void *arg) {
    (void) arg;
    ((void (*)(void)) (uintptr_t) s_header->start)();
}

void guest_start_game(void) { host_start_guest_thread((void (*)(void *)) start_thread, NULL); }

const FrameSnapshot *guest_frame_acquire(void) {
    return (const FrameSnapshot *) (uintptr_t) (uint32_t) guest_call(s_header->frame_acquire, 0, 0, 0);
}
void guest_snd_render(int16_t *out, int frames, int rate) {
    // The guest writes into a buffer it can address.
    static __thread int16_t *buf;
    static __thread int cap;
    if (frames > cap) {
        cap = frames < 4096 ? 4096 : frames;
        buf = host_low_alloc((size_t) cap * 4);
    }
    guest_call(s_header->snd_render, (uintptr_t) buf, (uint64_t) frames, (uint64_t) rate);
    memcpy(out, buf, (size_t) frames * 4);
}
void guest_input_set(const PortInput *in) { guest_call(s_header->input_set, (uintptr_t) in, 0, 0); }
void guest_save_flush(void) { guest_call(s_header->save_flush, 0, 0, 0); }
void guest_set_paused(bool paused) { *(volatile bool *) (uintptr_t) s_header->paused = paused; }
void guest_set_vblank_rate(uint32_t mhz) { *(volatile uint32_t *) (uintptr_t) s_header->vblank_rate = mhz; }
void guest_set_aspect(int32_t fx12) { *(volatile int32_t *) (uintptr_t) s_header->aspect = fx12; }
void guest_set_language(int language) { *(volatile int32_t *) (uintptr_t) s_header->language = language; }
void guest_set_player_name(const uint16_t *name) {
    memcpy((void *) (uintptr_t) s_header->player_name, name, PORT_PLAYER_NAME * sizeof(uint16_t));
}

uint64_t guest_frame_count(void) { return *(volatile uint64_t *) (uintptr_t) s_header->frame_count; }
void guest_set_lockstep(void *ready, void *done) {
    *(uint32_t *) (uintptr_t) s_header->lockstep_ready = (uint32_t) (uintptr_t) ready;
    *(uint32_t *) (uintptr_t) s_header->lockstep_done = (uint32_t) (uintptr_t) done;
}

// ------------------------------------------------------------------------------------------------ logging
#include <stdarg.h>
#include <stdio.h>

void port_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    hapi_log(line);
}

void port_fatal(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    port_log("FATAL: %s", line);
    abort();
}

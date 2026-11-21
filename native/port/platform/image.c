// The game image: what the DS's boot code and overlay loader do, for the parts of the image that stay at their DS
// addresses (data without source, see tools/gapgen.py), and the native parts that replace the rest.
//
// - port_image_load: the ARM9 binary from the ROM to its DS address, decompressed (MIi_UncompressBackward), the
//   autoload blocks (ITCM, DTCM) copied to their addresses, then the native pointers stored into the data without
//   source of the main module, ITCM and DTCM.
// - port_game_main: NitroStartUp, the main module's native static initializers, NitroMain (crt0's _start).
// - port_overlay_start / port_overlay_end (from the patched FS_StartOverlay / FS_EndOverlay): an overlay's native data
//   is reset as the DS reads it afresh from the ROM, the native pointers go into the overlay's data just loaded, its
//   static initializers run; on unload its static objects are destroyed.
#include "port.h"

#include <stdlib.h>
#include <string.h>

#include "hostapi.h"
#include "modules.h"

void MIi_UncompressBackward(void *compressedEnd);

static const uint8_t *s_rom;
static uint32_t s_rom_size;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

// Each entry: where, the native address, and the DS address that place holds when it still needs redirecting. Only
// such places change: a native object whose compiler already stored a native pointer (or whose layout differs from
// the DS one, as vtables do) is left alone.
static void apply_relocs(const uint32_t *table) {
    uint32_t n = table[0];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t *place = (uint32_t *) (uintptr_t) table[1 + 3 * i];
        if (*place == table[3 + 3 * i]) {
            *place = table[2 + 3 * i];
        }
    }
}

#if !defined(__ANDROID__) && !defined(GUEST_IMAGE)
#include <sys/mman.h>
extern char __executable_start[], __etext[];
#endif

void port_image_load(void) {
#if !defined(__ANDROID__) && !defined(GUEST_IMAGE)
    // The game writes to some of its constant data (tables patched at run time, and the relocations below).
    uintptr_t lo = (uintptr_t) __executable_start & ~(uintptr_t) 0xfff;
    extern char __port_main_init_start[];
    uintptr_t hi = ((uintptr_t) __port_main_init_start + 0xfff) & ~(uintptr_t) 0xfff;
    mprotect((void *) lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
    s_rom = hapi_rom(&s_rom_size);
    uint32_t arm9_rom = rd32(s_rom + 0x20), arm9_ram = rd32(s_rom + 0x28), arm9_size = rd32(s_rom + 0x2c);
    if ((uint64_t) arm9_rom + arm9_size > s_rom_size || arm9_ram < DS_MAIN_BASE) {
        port_fatal("image: bad ARM9 binary in the ROM header");
    }
    memcpy((void *) (uintptr_t) arm9_ram, s_rom + arm9_rom, arm9_size);
    // The module parameters (NitroSDK _start_ModuleParams): autoload list, list end, autoload source, static BSS
    // start and end, end of the compressed part.
    const uint32_t *params = (const uint32_t *) (uintptr_t) PORT_ARM9_MODULE_PARAMS;
    uint32_t autoload_list = params[0], autoload_list_end = params[1], autoload_src = params[2];
    uint32_t bss_start = params[3], bss_end = params[4], compressed_end = params[5];
    if (compressed_end) {
        MIi_UncompressBackward((void *) (uintptr_t) compressed_end);
        ((uint32_t *) (uintptr_t) PORT_ARM9_MODULE_PARAMS)[5] = 0;
    }
    for (uint32_t e = autoload_list; e < autoload_list_end; e += 12) {
        uint32_t dst = *(uint32_t *) (uintptr_t) e, size = *(uint32_t *) (uintptr_t) (e + 4);
        uint32_t bss = *(uint32_t *) (uintptr_t) (e + 8);
        memmove((void *) (uintptr_t) dst, (void *) (uintptr_t) autoload_src, size);
        memset((void *) (uintptr_t) (dst + size), 0, bss);
        autoload_src += size;
    }
    memset((void *) (uintptr_t) bss_start, 0, bss_end - bss_start);
    port_log("image: ARM9 %x bytes at %08x, bss %08x-%08x", arm9_size, arm9_ram, bss_start, bss_end);
    for (int m = 0; m < PORT_MODULE_COUNT; m++) {
        if (g_port_modules[m].overlay_id < 0) {
            apply_relocs(g_port_modules[m].relocs);
        }
    }
}

static void run_init(const PortModule *m) {
    for (void (**f)(void) = m->init_start; f < m->init_end; f++) {
        (*f)();
    }
}

void NitroStartUp(void);
void NitroMain(void) __asm__("_Z9NitroMainv");
void port_snapshot_overlays(void);

void port_game_main(void) {
    port_snapshot_overlays();
    NitroStartUp();
    for (int m = 0; m < PORT_MODULE_COUNT; m++) {
        if (g_port_modules[m].overlay_id < 0) {
            run_init(&g_port_modules[m]);
        }
    }
    NitroMain();
}

// ------------------------------------------------------------------------------------------------ overlays
static uint8_t *s_initial_data[PORT_MODULE_COUNT];

static const PortModule *overlay_module(uint32_t id, int *index) {
    for (int m = 0; m < PORT_MODULE_COUNT; m++) {
        if (g_port_modules[m].overlay_id == (int) id) {
            if (index) *index = m;
            return &g_port_modules[m];
        }
    }
    return NULL;
}

// Before anything runs: each overlay's native data as the compiler left it, to restore when the overlay loads.
void port_snapshot_overlays(void) {
    for (int m = 0; m < PORT_MODULE_COUNT; m++) {
        const PortModule *mod = &g_port_modules[m];
        size_t n = (size_t) (mod->data_end - mod->data_start);
        if (mod->overlay_id >= 0 && n) {
            s_initial_data[m] = malloc(n);
            memcpy(s_initial_data[m], mod->data_start, n);
        }
    }
}

void port_overlay_start(uint32_t id) {
    int index;
    const PortModule *m = overlay_module(id, &index);
    if (!m) {
        port_log("overlay: %u has no native module", id);
        return;
    }
    if (s_initial_data[index]) {
        memcpy(m->data_start, s_initial_data[index], (size_t) (m->data_end - m->data_start));
    }
    memset(m->bss_start, 0, (size_t) (m->bss_end - m->bss_start));
    apply_relocs(m->relocs);
    run_init(m);
}

// ------------------------------------------------------------------------------------------------ static objects
// The game's objects with destructors register them here (the game's objects call __cxa_atexit, renamed to this by
// the build), so that an overlay's objects can be destroyed when it unloads, as FS_EndOverlay does on the DS.
typedef struct Dtor {
    void (*fn)(void *);
    void *obj;
} Dtor;
static Dtor *s_dtors;
static int s_dtor_count, s_dtor_cap;
void *__dso_handle __attribute__((weak));

int port_cxa_atexit(void (*fn)(void *), void *obj, void *dso) {
    (void) dso;
    if (s_dtor_count == s_dtor_cap) {
        s_dtor_cap = s_dtor_cap ? s_dtor_cap * 2 : 256;
        s_dtors = realloc(s_dtors, sizeof(Dtor) * (size_t) s_dtor_cap);
    }
    s_dtors[s_dtor_count].fn = fn;
    s_dtors[s_dtor_count].obj = obj;
    s_dtor_count++;
    return 0;
}

void port_overlay_end(uint32_t id) {
    const PortModule *m = overlay_module(id, NULL);
    if (!m) return;
    for (int i = s_dtor_count; i-- > 0;) {
        uint8_t *obj = s_dtors[i].obj;
        bool own = (obj >= m->data_start && obj < m->data_end) || (obj >= m->bss_start && obj < m->bss_end);
        if (!own) continue;
        Dtor d = s_dtors[i];
        memmove(&s_dtors[i], &s_dtors[i + 1], sizeof(Dtor) * (size_t) (s_dtor_count - i - 1));
        s_dtor_count--;
        d.fn(d.obj);
    }
}

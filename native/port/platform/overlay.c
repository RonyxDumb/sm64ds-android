// Overlays. On the DS an overlay is read from the ROM into a shared memory area when the game loads it, so its data
// starts out fresh every time. Here every overlay is linked into the program (tools/portify.py puts each overlay's
// code and data into sections ovNNN_text/_rodata/_data/_bss); loading one restores its data and clears its BSS.
#include "port.h"

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "overlay_rom_loader.h"
#include "file_archive_access.h"
#include "file_handle_setup.h"
#include "Main/Net/slot_result_sync.h"
#include "overlay_section_symbols.h"
#include "overlay_id_symbols.h"

#define OVERLAY_SECTIONS(n)                                                                                            \
    extern u8 __start_ov##n##_text[] __attribute__((weak)), __stop_ov##n##_text[] __attribute__((weak));               \
    extern u8 __start_ov##n##_rodata[] __attribute__((weak)), __stop_ov##n##_rodata[] __attribute__((weak));           \
    extern u8 __start_ov##n##_data[] __attribute__((weak)), __stop_ov##n##_data[] __attribute__((weak));               \
    extern u8 __start_ov##n##_bss[] __attribute__((weak)), __stop_ov##n##_bss[] __attribute__((weak));
OVERLAY_SECTIONS(000)
OVERLAY_SECTIONS(001)
OVERLAY_SECTIONS(002)
OVERLAY_SECTIONS(003)

typedef struct Range {
    u8 *start, *stop;
} Range;
typedef struct Overlay {
    Range text, rodata, data, bss;
    u8 *initial_data; // copy of .data taken before the game starts
} Overlay;

#define R(n, s) {__start_ov##n##_##s, __stop_ov##n##_##s}
static Overlay s_overlays[4] = {
    {R(000, text), R(000, rodata), R(000, data), R(000, bss), NULL},
    {R(001, text), R(001, rodata), R(001, data), R(001, bss), NULL},
    {R(002, text), R(002, rodata), R(002, data), R(002, bss), NULL},
    {R(003, text), R(003, rodata), R(003, data), R(003, bss), NULL},
};

static size_t range_size(Range r) { return r.start && r.stop > r.start ? (size_t) (r.stop - r.start) : 0; }
static bool in_range(Range r, const void *p) { return r.start && (const u8 *) p >= r.start && (const u8 *) p < r.stop; }

void port_overlays_init(void) {
    for (int i = 0; i < 4; i++) {
        Overlay *ov = &s_overlays[i];
        size_t n = range_size(ov->data);
        if (n) {
            ov->initial_data = malloc(n);
            memcpy(ov->initial_data, ov->data.start, n);
        }
    }
}

static void reset_overlay(u32 id) {
    if (id >= 4) {
        port_log("overlay: unknown overlay %u", id);
        return;
    }
    Overlay *ov = &s_overlays[id];
    if (range_size(ov->data)) {
        memcpy(ov->data.start, ov->initial_data, range_size(ov->data));
    }
    if (range_size(ov->bss)) {
        memset(ov->bss.start, 0, range_size(ov->bss));
    }
}

void FS_ClearOverlayImage(FSOverlayInfo *info) { reset_overlay(info->mId); }

u32 FS_LoadOverlayImage(FSOverlayInfo *info) {
    reset_overlay(info->mId);
    return 1;
}

// The game polls the file for the end of the read and closes it: the file is opened but nothing is read.
u32 FS_LoadOverlayImageAsync(FSOverlayInfo *info, FSFile *file) {
    FSFileID fileID;
    FS_InitFile(file);
    FS_GetOverlayFileID(&fileID, info);
    if (FS_OpenFileFast(file, fileID) == 0) {
        return 0;
    }
    reset_overlay(info->mId);
    return 1;
}

// Static initialisers: the overlays of this game have none that the port needs to run.
void FS_StartOverlay(FSOverlayInfo *info) { (void) info; }

// Whether a map object descriptor is part of an overlay (the game skips the descriptors of overlays not loaded).
u32 IsInOverlayRange(struct MapObjectKindDescriptor *address) {
    for (int i = 0; i < 3; i++) { // the three overlays of the overlay loader table
        Overlay *ov = &s_overlays[i];
        if (in_range(ov->text, address) || in_range(ov->rodata, address) || in_range(ov->data, address) ||
            in_range(ov->bss, address)) {
            return 1;
        }
    }
    return 0;
}

// Linker-script symbols of the DS build. Only used as markers here.
u8 ARM9_TEXT_START[4];
u8 OV000_TEXT_START[4], OV001_TEXT_START[4], OV002_CTOR_START[4];
u8 OV000_BSS_END[4], OV001_BSS_END[4], OV002_BSS_END[4];
// Overlay ids are encoded in the address of these symbols.
#define OVERLAY_ID_SYMBOL(n) __asm__(".globl SDK_OVERLAY_OV00" #n "_ID\n.set SDK_OVERLAY_OV00" #n "_ID, " #n);
OVERLAY_ID_SYMBOL(0)
OVERLAY_ID_SYMBOL(1)
OVERLAY_ID_SYMBOL(2)
OVERLAY_ID_SYMBOL(3)
__asm__(".globl SDK_IRQ_STACKSIZE\n.set SDK_IRQ_STACKSIZE, 0x800");
__asm__(".globl SDK_SYS_STACKSIZE\n.set SDK_SYS_STACKSIZE, 0x1000");

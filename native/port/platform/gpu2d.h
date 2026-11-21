// Software renderer for the two 2D engines, and the VRAM bank mapping as the display hardware sees it.
#pragma once

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

// Where each 16 KiB page of the display-side VRAM regions comes from (offset into FrameSnapshot.vram, or -1).
enum {
    VR_BG_A,      // 0x06000000, 512 KiB
    VR_OBJ_A,     // 0x06400000, 256 KiB
    VR_BG_B,      // 0x06200000, 128 KiB
    VR_OBJ_B,     // 0x06600000, 128 KiB
    VR_LCDC,      // 0x06800000, 656 KiB (one page per bank page)
    VR_TEX,       // texture image slots 0..3, 512 KiB
    VR_TEXPAL,    // texture palette slots 0..5, 96 KiB
    VR_BGEXT_A,   // BG extended palettes, engine A, 32 KiB
    VR_OBJEXT_A,  // OBJ extended palette, engine A, 8 KiB
    VR_BGEXT_B,
    VR_OBJEXT_B,
    VR_REGION_COUNT
};

typedef struct VramMap {
    int32_t page[VR_REGION_COUNT][48]; // offset of each 16 KiB page in the bank block, -1 = unmapped
} VramMap;

void vram_map_build(VramMap *map, const uint8_t vramcnt[10]);
// Pointer to `offset` in region, or NULL. Offsets within a page are contiguous only up to the page end (16 KiB).
const uint8_t *vram_ptr(const VramMap *map, const uint8_t *vram, int region, uint32_t offset);

// Widest line: 256 DS columns plus up to 128 more on each side (widescreen).
#define GPU2D_MAX_W 512

// Renders both screens (RGBA, 0xAABBGGRR, 192 lines). layer3d: the 3D layer of engine A (256x192, alpha in 0..31 <<
// 24, colour in RGB888), NULL for none. The output order is top screen, bottom screen (POWCNT1 bit 15 chooses which
// engine). Widescreen: the top screen is 256 + 2 * top_margin wide (the bottom background layer and the backdrop
// continue to the sides), the bottom one 256.
void gpu2d_render(const FrameSnapshot *f, const uint32_t *layer3d, uint32_t *top, uint32_t *bottom, int top_margin);

// For a 3D layer rendered on the GPU: engine A's output split into what lies above the 3D layer and below it (see
// render_engine_line), engine B's output complete. Master brightness is not applied to engine A in split mode.
#define GPU2D_SPLIT ((const uint32_t *) (uintptr_t) 1)
void gpu2d_render_split(const FrameSnapshot *f, uint32_t *engine_a_above, uint32_t *engine_a_below,
                        uint32_t *engine_b, int margin_a, int margin_b);
bool gpu2d_engine_a_on_top(const FrameSnapshot *f);

// Software 3D rasteriser at the DS resolution (host tests and fallback).
void soft3d_render(const Gx3dFrame *frame, const FrameSnapshot *f, uint32_t *out);

// Display capture, as configured by DISPCAPCNT in the snapshot: writes into the live VRAM banks (g_vram_banks) so
// that the next snapshots see it. engineA: engine A's output line buffer (before master brightness), layer3d: 3D.
void gpu2d_capture(const FrameSnapshot *f, const uint32_t *engineA, const uint32_t *layer3d);
// Tells the game the capture is done (clears the enable bit of DISPCAPCNT; through the game process with ipc/).
extern void (*g_capture_done)(void);

#ifdef __cplusplus
}
#endif

// DS 2D engine scan-out.
//
// The 2D engines are pure register state: DISPCNT and BGxCNT say where the
// tilemaps and tiles live, and VRAM holds them. Nothing about drawing is
// write-triggered, so the host can simply read the register file once per frame
// and rasterise -- no interception needed, which is why 68% of the decomp's
// hardware accesses need no more than a working memory map to behave correctly.
//
// See notes/assessment.md section 2a and docs/mmio-inventory.md.

#ifndef NTR_PPU_H
#define NTR_PPU_H

#include <stdint.h>

namespace ntr {

constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 192;

enum Engine { ENGINE_A = 0, ENGINE_B = 1 };

struct Framebuffer {
    uint32_t px[SCREEN_H][SCREEN_W];   // 0xAARRGGBB
    // 0..3: topmost background/OBJ priority, 4: backdrop. Needed to
    // composite OBJ at its real DS priority rather than over every BG.
    uint8_t priority[SCREEN_H][SCREEN_W]{};
    // Visible BG layer 0..3, OBJ 4 or backdrop 5; needed for target-2 blends.
    uint8_t layer[SCREEN_H][SCREEN_W]{};
};

// Rasterise one engine's current register state into fb. Text-mode BGs only for
// now; affine, bitmap, sprites and the blend/window units are not implemented.
void ppu_scanout(Engine eng, Framebuffer &fb,const Framebuffer *geometry=nullptr);

// Rasterise one engine's OBJ layer (sprites) from its OAM + OBJ VRAM over
// whatever fb already holds. Plain and affine sprites, 16- and 256-color
// tiles, 1D mapping; bitmap OBJs, mosaic, windows and blending are not
// implemented. Transparent color-0 texels leave fb untouched.
void ppu_scanout_obj(Engine eng, Framebuffer &fb);
void ppu_capture(const Framebuffer &composited,const Framebuffer &geometry);

// Debug output, so a frame can be inspected without a window yet.
bool ppu_write_bmp(const char *path, const Framebuffer &fb);

}  // namespace ntr

#endif  // NTR_PPU_H

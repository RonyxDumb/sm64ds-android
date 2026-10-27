#include "ntr/mmio.h"
#include "ntr/ppu.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint32_t convert(uint16_t color) {
    const uint32_t r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
    return 0xff000000u | ((r * 255 / 31) << 16) |
           ((g * 255 / 31) << 8) | (b * 255 / 31);
}

int main() {
    assert(ntr::io_init());
    ntr::Framebuffer frame{};
    // DISPCNT display mode 2 selects the LCDC bank directly, not tile BGs.
    auto *lcdc = static_cast<uint16_t *>(ntr::host_ptr(0x06800000u));
    for (unsigned i = 0; i < 256u * 192u; ++i)
        lcdc[i] = i % 2 ? 0x001f : 0x7c00;
    ntr::io_write(0x04000000u, (2u << 16) | (1u << 10) | 3u, 4);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x7c00));
    assert(frame.px[0][1] == convert(0x001f));
    assert(frame.px[191][255] == convert(0x001f));

    // Mode 3 is an affine BG2, not a text BG. Map a single 8bpp tile
    // through the real bank-routing path; the correct output is red.
    ntr::io_write(0x04000240u, 0x81, 1); // bank A -> engine A BG
    auto *vram = static_cast<uint8_t *>(ntr::host_ptr(0x06000000u));
    std::memset(vram, 0, 0x8000);
    vram[0x800] = 1;            // screen base 1, affine map tile 1
    vram[0x4000 + 64] = 1;     // char base 1, tile 1, first texel
    auto *palette = static_cast<uint16_t *>(ntr::host_ptr(0x05000000u));
    palette[0] = 0;
    palette[1] = 0x001f;
    ntr::io_write(0x0400000cu, (1u << 8) | (1u << 2), 2);
    ntr::io_write(0x04000020u, 256, 2); // BG2PA
    ntr::io_write(0x04000022u, 0, 2);
    ntr::io_write(0x04000024u, 0, 2);
    ntr::io_write(0x04000026u, 256, 2); // BG2PD
    ntr::io_write(0x04000028u, 0, 4); // BG2X
    ntr::io_write(0x0400002cu, 0, 4); // BG2Y
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 10) | 3u, 4);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x001f));
    // OBJ priority is compared against the visible BG pixel, not layered
    // indiscriminately over it. When equal, OBJ wins; lower BG priority wins.
    ntr::io_write(0x04000241u, 0x82, 1);  // VRAM B -> engine A OBJ
    auto *obj = static_cast<uint8_t *>(ntr::host_ptr(0x06400000u));
    std::memset(obj, 0, 0x100);
    obj[32] = 1; // first texel of sprite tile 1, 4bpp
    auto *objPalette = static_cast<uint16_t *>(ntr::host_ptr(0x05000200u));
    objPalette[1] = 0x001f;
    std::memset(vram, 0, 0x8000);
    vram[0x800] = 1; // text BG0 tile 1
    vram[0x4000 + 32] = 2; // first 4bpp texel of BG tile 1, palette index 2
    palette[2] = 0x03e0; // green
    ntr::io_write(0x04000008u, 1u | (1u << 8) | (1u << 2), 2); // BG0 priority 1
    auto *oam = static_cast<uint16_t *>(ntr::host_ptr(0x07000000u));
    for (unsigned i = 0; i < 128; ++i) oam[i * 4] = 0x0200; // disable
    oam[0] = 0; // sprite at (0,0)
    oam[1] = 0;
    oam[2] = 1u | (2u << 10); // OBJ priority 2, behind BG0
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 8) | (1u << 12), 4);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x03e0) && frame.priority[0][0] == 1);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x03e0));
    oam[2] = 1u; // OBJ priority 0, in front of BG0
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x001f) && frame.priority[0][0] == 0);
    oam[2] = 1u | (1u << 10); // priority tie: OBJ in front
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x001f));
    // Master brightness applies to hardware sprites too, not only the BG.
    ntr::io_write(0x0400006cu, (2u << 14) | 16u, 2);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == 0xff000000u);
    ntr::io_write(0x0400006cu, 0, 2);
    // NDS OAM mode 2 is an invisible OBJ-window mask, not a paletted sprite.
    oam[0] = 2u << 10;
    oam[2] = 1;
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x03e0));

    // OAM mode 3 selects a direct-color bitmap. In 1D mapping, character
    // index 1 begins at a 128-byte boundary; pixel bit15 selects opacity.
    oam[0] = 3u << 10;
    oam[2] = 1u | (15u << 12);
    auto *bitmap = static_cast<uint16_t *>(ntr::host_ptr(0x06400000u));
    bitmap[64] = 0x801f;
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 8) |
                  (1u << 12) | (1u << 6), 4);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x001f));
    oam[2] = 1u; // Alpha-OAM=0 hides bitmap even if pixel bit15 is set
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x03e0));
    std::puts("NDS bitmap, affine BG2, OBJ priority, bitmap OAM and master fade passed.");
}

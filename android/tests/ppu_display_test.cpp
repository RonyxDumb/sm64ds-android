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
    std::puts("NDS VRAM bitmap display and mode-3 affine BG2 passed.");
}

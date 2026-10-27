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

    // Mode 4 makes BG2 affine; mode 3 leaves BG2 as tiled text.
    // Map a single 8bpp affine tile through the real bank-routing path.
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
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 10) | 4u, 4);
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
    // A semi-transparent OBJ ignores the global first-target selection, but
    // must blend only when the underneath layer is a selected second target.
    oam[0] = 1u << 10;
    oam[2] = 1;
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 8) | (1u << 12), 4);
    ntr::io_write(0x04000050u, 1u << 8, 2); // only BG0 as second target
    ntr::io_write(0x04000052u, 8u | (8u << 8), 2); // 50% + 50%
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    ntr::ppu_scanout_obj(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == 0xff808000u);
    ntr::io_write(0x04000050u, 0, 2);

    // Genuine BLDCNT effect 1: first-target BG0 (green) blends with the
    // immediately underlying second-target BG1 (red), both as 4bpp tiles.
    oam[0] = 0x0200; // no OBJ covers the test pixel
    vram[0x1000] = 2; // BG1 screen block 2, tile 2
    vram[0x4000 + 64] = 1; // tile 2 uses palette color 1, red
    ntr::io_write(0x0400000au, 2u | (2u << 8) | (1u << 2), 2);
    ntr::io_write(0x04000000u, (1u << 16) | (1u << 8) | (1u << 9), 4);
    ntr::io_write(0x04000050u, (1u << 6) | 1u | (1u << 9), 2);
    ntr::io_write(0x04000052u, 8u | (8u << 8), 2);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == 0xff808000u);
    // BG1 is no longer a second target: no blending may take place.
    ntr::io_write(0x04000050u, (1u << 6) | 1u | (1u << 10), 2);
    ntr::ppu_scanout(ntr::ENGINE_A, frame);
    assert(frame.px[0][0] == convert(0x03e0));
    ntr::io_write(0x04000050u, 0, 2);

    // DS SUB engine mode 3 has BG2 TEXT and BG3 EXTENDED. The old
    // renderer incorrectly decoded BG2 as an 8-bit affine screen and could
    // turn title/options backgrounds into narrow vertical colored stripes.
    // Mode 4 uses the SAME BG2 as AFFINE: test both on real VRAM bytes.
    ntr::io_write(0x04000242u, 0x84, 1); // VRAM C -> sub BG
    auto *subvram = static_cast<uint8_t *>(ntr::host_ptr(0x06200000u));
    std::memset(subvram, 0, 0x8000);
    auto *subpal = static_cast<uint16_t *>(ntr::host_ptr(0x05000400u));
    subpal[0] = 0;
    subpal[2] = 0x7c00; // blue: 4bpp TEXT BG2
    subpal[3] = 0x03e0; // green: 8bpp AFFINE BG2
    subvram[0x800] = 2; // text entry tile 2; affine entry tile 2
    subvram[0x4000 + 2 * 32] = 2;
    subvram[0x4000 + 2 * 64] = 3;
    ntr::io_write(0x0400100cu, (1u << 8) | (1u << 2), 2);
    ntr::io_write(0x04001020u, 256, 2); // affine identity (only mode 4)
    ntr::io_write(0x04001022u, 0, 2);
    ntr::io_write(0x04001024u, 0, 2);
    ntr::io_write(0x04001026u, 256, 2);
    ntr::io_write(0x04001028u, 0, 4);
    ntr::io_write(0x0400102cu, 0, 4);
    ntr::io_write(0x04001000u, (1u << 16) | (1u << 10) | 3u, 4);
    ntr::ppu_scanout(ntr::ENGINE_B, frame);
    assert(frame.px[0][0] == convert(0x7c00));
    ntr::io_write(0x04001000u, (1u << 16) | (1u << 10) | 4u, 4);
    ntr::ppu_scanout(ntr::ENGINE_B, frame);
    assert(frame.px[0][0] == convert(0x03e0));

    // A recovered DS overlay may write VRAMCNT_H with a *volatile byte
    // pointer*, bypassing the io_write proxy. The LCD refresh must observe
    // that change and route physical bank H to the sub-engine BG window.
#if !defined(_WIN32)
    ntr::io_write(0x04000242u, 0, 1); // unroute bank C from sub BG
    auto *physicalH = static_cast<uint8_t *>(ntr::host_ptr(0x06898000u));
    physicalH[0] = 0x6b;
    auto *directControl = static_cast<volatile uint8_t *>(ntr::host_ptr(0x04000248u));
    *directControl = 0x81; // bank H -> sub BG, no io_write call
    ntr::ppu_scanout(ntr::ENGINE_B, frame);
    assert(*static_cast<const uint8_t *>(ntr::host_ptr(0x06200000u)) == 0x6b);
    *directControl = 0; // direct write must also unmap the alias
    ntr::ppu_scanout(ntr::ENGINE_B, frame);
    assert(*static_cast<const uint8_t *>(ntr::host_ptr(0x06200000u)) != 0x6b);
    assert(physicalH[0] == 0x6b);
#endif
    std::puts("NDS BG modes, sprite blending and native VRAM bank routing passed.");
}

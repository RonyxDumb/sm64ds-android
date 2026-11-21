// DS texture formats: decode a whole texture to RGBA (alpha 0..31 in the top byte), shared by the software
// rasteriser and the OpenGL renderer's texture cache.
#include "port.h"
#include "tex.h"

static inline uint32_t rgba(uint16_t c, uint32_t a) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | a << 24;
}

static inline uint8_t tex8(const VramMap *m, const uint8_t *vram, uint32_t off) {
    const uint8_t *p = vram_ptr(m, vram, VR_TEX, off & 0x7ffff);
    return p ? *p : 0;
}

static inline uint16_t tex16(const VramMap *m, const uint8_t *vram, uint32_t off) {
    return (uint16_t) (tex8(m, vram, off) | tex8(m, vram, off + 1) << 8);
}

static inline uint16_t pal16(const VramMap *m, const uint8_t *vram, uint32_t off) {
    const uint8_t *p = vram_ptr(m, vram, VR_TEXPAL, off & 0x1ffff);
    return p ? (uint16_t) (p[0] | p[1] << 8) : 0;
}

bool tex_decode(const VramMap *m, const uint8_t *vram, uint32_t teximage, uint32_t pltt, uint32_t *out) {
    uint32_t fmt = (teximage >> 26) & 7;
    if (fmt == 0) {
        return false;
    }
    uint32_t w = TEX_WIDTH(teximage), h = TEX_HEIGHT(teximage);
    uint32_t addr = (teximage & 0xffff) << 3;
    bool color0_transparent = teximage & (1u << 29);
    uint32_t pal = fmt == 2 ? pltt << 3 : pltt << 4;
    switch (fmt) {
    case 1: // A3I5
        for (uint32_t i = 0; i < w * h; i++) {
            uint8_t v = tex8(m, vram, addr + i);
            uint32_t a3 = v >> 5;
            out[i] = rgba(pal16(m, vram, pal + (v & 31) * 2), (a3 << 2) + (a3 >> 1));
        }
        break;
    case 2: // 4 colours
        for (uint32_t i = 0; i < w * h; i++) {
            uint32_t idx = (tex8(m, vram, addr + i / 4) >> ((i & 3) * 2)) & 3;
            out[i] = rgba(pal16(m, vram, pal + idx * 2), idx == 0 && color0_transparent ? 0 : 31);
        }
        break;
    case 3: // 16 colours
        for (uint32_t i = 0; i < w * h; i++) {
            uint32_t idx = (tex8(m, vram, addr + i / 2) >> ((i & 1) * 4)) & 15;
            out[i] = rgba(pal16(m, vram, pal + idx * 2), idx == 0 && color0_transparent ? 0 : 31);
        }
        break;
    case 4: // 256 colours
        for (uint32_t i = 0; i < w * h; i++) {
            uint32_t idx = tex8(m, vram, addr + i);
            out[i] = rgba(pal16(m, vram, pal + idx * 2), idx == 0 && color0_transparent ? 0 : 31);
        }
        break;
    case 5: { // 4x4 compressed: 2-bit texels per block, palette index data in slot 1
        uint32_t slot1 = 0x20000 + ((addr & 0x1ffff) >> 1) + (addr >= 0x40000 ? 0x10000 : 0);
        for (uint32_t by = 0; by < h / 4; by++) {
            for (uint32_t bx = 0; bx < w / 4; bx++) {
                uint32_t block = by * (w / 4) + bx;
                uint32_t bits = tex8(m, vram, addr + block * 4) | tex8(m, vram, addr + block * 4 + 1) << 8 |
                                tex8(m, vram, addr + block * 4 + 2) << 16 |
                                (uint32_t) tex8(m, vram, addr + block * 4 + 3) << 24;
                uint16_t info = tex16(m, vram, slot1 + block * 2);
                uint32_t pbase = pal + (info & 0x3fff) * 4;
                uint32_t mode = info >> 14;
                uint16_t c0 = pal16(m, vram, pbase), c1 = pal16(m, vram, pbase + 2);
                uint32_t cols[4];
                cols[0] = rgba(c0, 31);
                cols[1] = rgba(c1, 31);
                switch (mode) {
                case 0:
                    cols[2] = rgba(pal16(m, vram, pbase + 4), 31);
                    cols[3] = 0;
                    break;
                case 1: {
                    uint16_t mix = 0;
                    for (int s = 0; s < 15; s += 5) mix |= (uint16_t) ((((c0 >> s) & 31) + ((c1 >> s) & 31)) / 2 << s);
                    cols[2] = rgba(mix, 31);
                    cols[3] = 0;
                    break;
                }
                case 2:
                    cols[2] = rgba(pal16(m, vram, pbase + 4), 31);
                    cols[3] = rgba(pal16(m, vram, pbase + 6), 31);
                    break;
                default: {
                    uint16_t m1 = 0, m2 = 0;
                    for (int s = 0; s < 15; s += 5) {
                        uint32_t a = (c0 >> s) & 31, b = (c1 >> s) & 31;
                        m1 |= (uint16_t) ((a * 5 + b * 3) / 8 << s);
                        m2 |= (uint16_t) ((a * 3 + b * 5) / 8 << s);
                    }
                    cols[2] = rgba(m1, 31);
                    cols[3] = rgba(m2, 31);
                    break;
                }
                }
                for (uint32_t ty = 0; ty < 4; ty++) {
                    for (uint32_t tx = 0; tx < 4; tx++) {
                        out[(by * 4 + ty) * w + bx * 4 + tx] = cols[(bits >> ((ty * 4 + tx) * 2)) & 3];
                    }
                }
            }
        }
        break;
    }
    case 6: // A5I3
        for (uint32_t i = 0; i < w * h; i++) {
            uint8_t v = tex8(m, vram, addr + i);
            out[i] = rgba(pal16(m, vram, pal + (v & 7) * 2), v >> 3);
        }
        break;
    case 7: // direct colour
        for (uint32_t i = 0; i < w * h; i++) {
            uint16_t c = tex16(m, vram, addr + i * 2);
            out[i] = rgba(c, c & 0x8000 ? 31 : 0);
        }
        break;
    }
    return true;
}

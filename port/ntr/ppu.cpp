// DS 2D engine, text-mode backgrounds.
//
// Register layout from GBATEK. The address arithmetic is cross-checked against
// the decomp's own G2::GetBG0ScrPtr, which computes
//
//     0x6000000 + ((DISPCNT & 0x38000000) >> 27 << 16) + ((BG0CNT & 0x1f00) >> 8 << 11)
//
// i.e. DISPCNT bits 27-29 select a 64K screen-base offset and BGxCNT bits 8-12 a
// 2K screen block. Those two shifts are the ones most likely to be wrong, so they
// are taken from code that is byte-verified against the ROM rather than from docs.

#include "ntr/ppu.h"

#include "ntr/mmio.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <array>
#include <stdexcept>

namespace ntr {
namespace {

// Engine A registers start at 0x4000000, engine B at 0x4001000. Engine B has no
// DISPCNT char/screen base offset -- those bits are engine A only -- and its BG
// VRAM and palette live elsewhere.
struct EngineMap {
    uint32_t reg_base;
    uint32_t vram_base;
    uint32_t pltt_base;
    bool has_base_offset;
};

constexpr EngineMap kEngines[2] = {
    {0x04000000u, 0x06000000u, 0x05000000u, true},
    {0x04001000u, 0x06200000u, 0x05000400u, false},
};

inline uint8_t rd8(uint32_t a) { return *static_cast<volatile uint8_t *>(host_ptr(a)); }
inline uint16_t rd16(uint32_t a) { return *static_cast<volatile uint16_t *>(host_ptr(a)); }
inline uint32_t rd32(uint32_t a) { return *static_cast<volatile uint32_t *>(host_ptr(a)); }

// DS palettes are BGR555.
inline uint32_t bgr555(uint16_t c) {
    const uint32_t r = (c >> 0) & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8)
           | (b << 3 | b >> 2);
}

// Text-mode screen sizes, in tiles.
void text_size(unsigned sz, int &tw, int &th) {
    tw = (sz & 1) ? 64 : 32;
    th = (sz & 2) ? 64 : 32;
}

struct BgConfig {
    bool enabled;
    int priority;
    uint32_t screen;      // tilemap address
    uint32_t chars;       // tile data address
    bool bpp8;
    int tiles_w, tiles_h;
    int hofs, vofs;
    enum Kind { Text, Affine, Extended, Bitmap, Large } kind=Text;
    bool direct=false,wrap=false;
    int pa=0,pb=0,pc=0,pd=0,refx=0,refy=0;
    int width=0,height=0;
};

BgConfig read_bg(const EngineMap &em, int bg, uint32_t dispcnt) {
    BgConfig c{};
    c.enabled = (dispcnt >> (8 + bg)) & 1;
    if (!c.enabled) return c;

    const uint16_t cnt = rd16(em.reg_base + 0x08 + bg * 2);
    c.priority = cnt & 3;
    c.bpp8 = (cnt >> 7) & 1;

    const uint32_t char_off = em.has_base_offset ? ((dispcnt >> 24) & 7) << 16 : 0;
    const uint32_t scr_off = em.has_base_offset ? ((dispcnt >> 27) & 7) << 16 : 0;

    c.screen = em.vram_base + scr_off + (((cnt >> 8) & 0x1F) << 11);
    c.chars = em.vram_base + char_off + (((cnt >> 2) & 15) << 14);

    text_size((cnt >> 14) & 3, c.tiles_w, c.tiles_h);
    const unsigned mode=dispcnt&7,size=cnt>>14;
    // DS hardware BG mode table: in mode 3 BG2 remains TEXT; BG3 is
    // extended. Only modes 2 and 4 make BG2 affine. Misclassifying BG2
    // as affine reads 8-bit tile-map bytes as rotated pixels and causes
    // striped/corrupted lower-screen menus.
    if ((bg==3 && (mode==1 || mode==2)) ||
        (bg==2 && (mode==2 || mode==4))) c.kind=BgConfig::Affine;
    if ((bg==3 && mode>=3 && mode<=5) || (bg==2 && mode==5)) c.kind=BgConfig::Extended;
    if (c.kind==BgConfig::Extended && (cnt&0x80)) c.kind=BgConfig::Bitmap;
    if (mode==6) {
        if (bg!=2 || !em.has_base_offset) { c.enabled=false; return c; }
        c.kind=BgConfig::Large;
    }
    if (c.kind!=BgConfig::Text) {
        c.width=c.height=128<<size;
        if (c.kind==BgConfig::Bitmap) {
            constexpr int widths[]={128,256,512,512},heights[]={128,256,256,512};
            c.width=widths[size]; c.height=heights[size]; c.direct=cnt&4;
            c.screen=em.vram_base+(((cnt>>8)&31)<<14);
        } else if (c.kind==BgConfig::Large) {
            c.width=(cnt&0x4000)?1024:512; c.height=(cnt&0x4000)?512:1024;
            c.screen=em.vram_base;
        }
        c.tiles_w=c.width/8; c.tiles_h=c.height/8; c.wrap=cnt&0x2000;
        const unsigned matrix=em.reg_base+0x20+(bg-2)*16;
        c.pa=static_cast<int16_t>(rd16(matrix)); c.pb=static_cast<int16_t>(rd16(matrix+2));
        c.pc=static_cast<int16_t>(rd16(matrix+4)); c.pd=static_cast<int16_t>(rd16(matrix+6));
        c.refx=static_cast<int32_t>(rd32(matrix+8)<<4)>>4;
        c.refy=static_cast<int32_t>(rd32(matrix+12)<<4)>>4;
        c.bpp8=true;
    }

    // BGxHOFS/BGxVOFS are write-only 9-bit scroll registers, 4 bytes per BG.
    c.hofs = rd16(em.reg_base + 0x10 + bg * 4) & 0x1FF;
    c.vofs = rd16(em.reg_base + 0x12 + bg * 4) & 0x1FF;
    return c;
}

// One text-mode screen entry: tile 0-9, hflip 10, vflip 11, palette 12-15.
inline uint32_t screen_entry_addr(const BgConfig &c, int tx, int ty) {
    // 512-wide/tall maps are stored as consecutive 32x32 blocks of 2KB.
    const int bx = tx >> 5, by = ty >> 5;
    const int blocks_w = c.tiles_w >> 5;
    const int block = by * blocks_w + bx;
    return c.screen + block * 0x800 + (((ty & 31) * 32 + (tx & 31)) * 2);
}

// Returns false where the background is transparent at this pixel.
bool sample_bg(const BgConfig &c, const uint32_t *palette, int x, int y, uint32_t &out) {
    int px=(x+c.hofs)&(c.tiles_w*8-1),py=(y+c.vofs)&(c.tiles_h*8-1);
    if (c.kind!=BgConfig::Text) {
        px=static_cast<int>((int64_t(c.refx)+c.pa*x+c.pb*y)>>8);
        py=static_cast<int>((int64_t(c.refy)+c.pc*x+c.pd*y)>>8);
        if (c.wrap) { px&=c.width-1; py&=c.height-1; }
        else if (px<0 || px>=c.width || py<0 || py>=c.height) return false;
        if (c.kind==BgConfig::Bitmap || c.kind==BgConfig::Large) {
            const unsigned pixel=unsigned(py*c.width+px);
            if (c.direct) {
                const uint16_t value=rd16(c.screen+pixel*2);
                if (!(value&0x8000)) return false;
                out=bgr555(value);
            } else {
                const uint8_t index=rd8(c.screen+pixel);
                if (!index) return false;
                out=palette[index];
            }
            return true;
        }
    }

    uint16_t se;
    if (c.kind==BgConfig::Affine)
        se=rd8(c.screen+(py>>3)*c.tiles_w+(px>>3));
    else if (c.kind==BgConfig::Extended)
        se=rd16(c.screen+((py>>3)*c.tiles_w+(px>>3))*2);
    else se=rd16(screen_entry_addr(c,px>>3,py>>3));
    const int tile = se & 0x3FF;
    int fx = px & 7, fy = py & 7;
    if (se & 0x400) fx = 7 - fx;
    if (se & 0x800) fy = 7 - fy;

    uint32_t index;
    if (c.bpp8) {
        index = rd8(c.chars + tile * 64 + fy * 8 + fx);
        if (index == 0) return false;
    } else {
        const uint8_t pair =
            rd8(c.chars + tile * 32 + fy * 4 + (fx >> 1));
        index = (fx & 1) ? (pair >> 4) : (pair & 0xF);
        if (index == 0) return false;
        index += ((se >> 12) & 0xF) * 16;
    }
    out = palette[index];
    return true;
}

}  // namespace

void ppu_scanout(Engine eng, Framebuffer &fb,const Framebuffer *geometry) {
    // Direct native/overlay writes to VRAMCNT bypass io_write; resynchronize
    // bank routing before any BG tile, bitmap or palette access.
    sync_vram_banks();
    const EngineMap &em = kEngines[eng];
    const uint32_t dispcnt = rd32(em.reg_base);

    // Display mode 0 is "display off" -- the DS shows white, not black.
    const unsigned disp_mode = (dispcnt >> 16) & 3;
    const bool forced_blank = (dispcnt >> 7) & 1;
    if (disp_mode == 0 || forced_blank) {
        for (int y = 0; y < SCREEN_H; ++y)
            for (int x = 0; x < SCREEN_W; ++x) {
                fb.px[y][x] = 0xFFFFFFFFu;
                fb.priority[y][x] = 4;
                fb.layer[y][x] = 5;
            }
        return;
    }

    BgConfig bgs[4];
    for (int i = 0; i < 4; ++i) bgs[i] = read_bg(em, i, dispcnt);
    const bool bg0_3d=eng==ENGINE_A && (dispcnt&8);
    // Prepare the DS priority order once, not 16 branches per pixel.
    // Backgrounds with higher priority numbers draw first; BG0 wins ties.
    int order[4]{};
    unsigned orderCount = 0;
    for (int priority = 3; priority >= 0; --priority)
        for (int bg = 3; bg >= 0; --bg)
            if (bgs[bg].enabled && bgs[bg].priority == priority)
                order[orderCount++] = bg;

    // Background 4/8bpp palettes are immutable for this scanout frame.
    // Convert once rather than repeating bus lookups and BGR555 expansion
    // for every pixel of every visible tile.
    std::array<uint32_t, 256> bgPalette{};
    for (unsigned i = 0; i < bgPalette.size(); ++i)
        bgPalette[i] = bgr555(rd16(em.pltt_base + i * 2u));
    const uint32_t backdrop = bgPalette[0];
    const uint16_t blend = rd16(em.reg_base + 0x50), master = rd16(em.reg_base + 0x6c);
    const unsigned blend_mode = (blend >> 6) & 3, master_mode = (master >> 14) & 3;
    const uint16_t blendAlpha = rd16(em.reg_base + 0x52);
    const unsigned blendEVA = std::min(16u, unsigned(blendAlpha & 31u));
    const unsigned blendEVB = std::min(16u, unsigned((blendAlpha >> 8) & 31u));
    const unsigned blend_level = std::min(16u, unsigned(rd16(em.reg_base + 0x54) & 31));
    const unsigned master_level = std::min(16u, unsigned(master & 31));
    auto brightness = [](uint32_t color, unsigned mode, unsigned level) {
        uint32_t result = 0xff000000;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            unsigned value = (color >> shift) & 255;
            if (mode == 1) value += ((255 - value) * level) / 16;
            if (mode == 2) value -= (value * level) / 16;
            result |= value << shift;
        }
        return result;
    };

    // DISPCNT mode 2 bypasses ALL background, OBJ and 3D layers. The
    // selected LCDC VRAM A-D bank is a direct 256x192 BGR555 bitmap.
    // Previously this path decoded the bitmap as BG tile maps, producing
    // the narrow repeated vertical stripes seen in the Android title.
    if (eng == ENGINE_A && disp_mode == 2) {
        const uint32_t base = 0x06800000u + ((dispcnt >> 18) & 3u) * 0x20000u;
        const uint16_t *pixels = static_cast<const uint16_t *>(host_ptr(base));
        for (unsigned i = 0; i < SCREEN_W * SCREEN_H; ++i) {
            uint32_t c = bgr555(pixels[i]);
            if (master_mode == 1 || master_mode == 2)
                c = brightness(c, master_mode, master_level);
            fb.px[i / SCREEN_W][i % SCREEN_W] = c;
            fb.priority[i / SCREEN_W][i % SCREEN_W] = 4;
            fb.layer[i / SCREEN_W][i % SCREEN_W] = 5;
        }
        return;
    }

    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            uint32_t c = backdrop;
            uint32_t behind = backdrop;
            unsigned layer = 5, behindLayer = 5, topPriority = 4;
            bool topIs3D = false;
            // Lower priority value wins; ties break toward the lower BG number,
            // so walk priorities 3..0 and let later writes overwrite.
            for (unsigned item = 0; item < orderCount; ++item) {
                const int bg = order[item];
                uint32_t s;
                if (bg == 0 && bg0_3d) {
                    if (!geometry) continue;
                    const uint32_t pixel = geometry->px[y][x];
                    const unsigned alpha = pixel >> 24;
                    if (!alpha) continue;
                    s = 0xff000000;
                    for (unsigned shift = 0; shift < 24; shift += 8)
                        s |= ((((pixel >> shift) & 255) * alpha +
                               ((c >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
                    behind = c;
                    behindLayer = layer;
                    c = s;
                    layer = 0;
                    topIs3D = true;
                    topPriority = unsigned(bgs[bg].priority);
                } else if (sample_bg(bgs[bg], bgPalette.data(), x, y, s)) {
                    behind = c;
                    behindLayer = layer;
                    c = s;
                    layer = unsigned(bg);
                    topIs3D = false;
                    topPriority = unsigned(bgs[bg].priority);
                }
            }
            // BLDCNT effect 1 blends the TOP visible first-target BG with
            // the NEXT visible second-target layer. Layers hidden between
            // them cannot be skipped. Do not apply this a second time to BG0
            // 3D pixels, whose alpha was already composited above.
            if (blend_mode == 1 && !topIs3D &&
                (blend & (1u << layer)) && (blend & (1u << (8u + behindLayer)))) {
                uint32_t mixed = 0xff000000u;
                for (unsigned shift = 0; shift < 24; shift += 8) {
                    const unsigned first = (c >> shift) & 255u;
                    const unsigned second = (behind >> shift) & 255u;
                    mixed |= std::min(255u, (first * blendEVA +
                                              second * blendEVB + 8u) / 16u) << shift;
                }
                c = mixed;
            } else if ((blend & (1u << layer)) && (blend_mode == 2 || blend_mode == 3)) {
                c = brightness(c, blend_mode == 2 ? 1 : 2, blend_level);
            }
            if (master_mode == 1 || master_mode == 2) c = brightness(c, master_mode, master_level);
            fb.px[y][x] = c;
            fb.priority[y][x] = static_cast<uint8_t>(topPriority);
            fb.layer[y][x] = static_cast<uint8_t>(layer);
        }
    }
}

// --- OBJ layer --------------------------------------------------------------
// GBATEK "LCD OBJ": 128 OAM entries per engine; tiles in the engine's OBJ
// VRAM region, palette at pltt+0x200. Sizes come from the shape/size table.
// Affine params sit in the filler word of entries group*4+0..3 (pa,pb,pc,pd).
void ppu_scanout_obj(Engine eng, Framebuffer &fb) {
    sync_vram_banks();
    static const int kSizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},    // square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},    // wide
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},    // tall
    };
    const uint32_t oam_base = eng == ENGINE_A ? 0x07000000u : 0x07000400u;
    const uint32_t obj_vram = eng == ENGINE_A ? 0x06400000u : 0x06600000u;
    const uint32_t obj_pltt = kEngines[eng].pltt_base + 0x200u;
    // DISPCNT bit 4: OBJ 1D mapping; bits 20-21: tile-index boundary shift.
    const uint32_t dispcnt = rd32(kEngines[eng].reg_base);
    if (!(dispcnt&(1u<<12)) || (dispcnt&0x80) || ((dispcnt>>16)&3)!=1) return;
    const uint32_t boundary = 32u << ((dispcnt >> 20) & 3);
    const uint16_t blendControl = rd16(kEngines[eng].reg_base + 0x50);
    const uint16_t blendAlpha = rd16(kEngines[eng].reg_base + 0x52);
    const unsigned eva = std::min(16u, unsigned(blendAlpha & 31));
    const unsigned evb = std::min(16u, unsigned((blendAlpha >> 8) & 31));
    auto blendColors = [](uint32_t fg, uint32_t bg, unsigned fa, unsigned ba) {
        uint32_t result = 0xff000000u;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const unsigned component = (((fg >> shift) & 255u) * fa +
                                        ((bg >> shift) & 255u) * ba + 8u) / 16u;
            result |= std::min(255u, component) << shift;
        }
        return result;
    };

    // A 256-entry palette cache eliminates thousands of per-sprite MMIO
    // lookups and BGR555 conversions on the CPU. The DS master-brightness
    // register acts after BG/OBJ compositing; BGs have already received it
    // in ppu_scanout, so OBJ colors need the same transform before overlay.
    const uint16_t master = rd16(kEngines[eng].reg_base + 0x6c);
    const unsigned masterMode = (master >> 14) & 3;
    const unsigned masterLevel = std::min(16u, unsigned(master & 31u));
    std::array<uint32_t, 256> colors{};
    for (unsigned i = 0; i < colors.size(); ++i) {
        uint32_t color = bgr555(rd16(obj_pltt + i * 2));
        if (masterMode == 1 || masterMode == 2) {
            uint32_t transformed = 0xff000000u;
            for (unsigned shift = 0; shift < 24; shift += 8) {
                unsigned component = (color >> shift) & 255u;
                if (masterMode == 1)
                    component += ((255u - component) * masterLevel) / 16u;
                else
                    component -= component * masterLevel / 16u;
                transformed |= component << shift;
            }
            color = transformed;
        }
        colors[i] = color;
    }

    for (int i = 127; i >= 0; --i) {           // low index on top: draw high first
        const uint16_t a0 = rd16(oam_base + i * 8u);
        const uint16_t a1 = rd16(oam_base + i * 8u + 2);
        const uint16_t a2 = rd16(oam_base + i * 8u + 4);
        const bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue;             // disabled
        const unsigned objMode = (a0 >> 10) & 3;
        if (objMode == 2) continue; // OBJ window is a mask, never visible pixels
        const bool bitmap = objMode == 3; // DS direct-color Bitmap OBJ
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int size = (a1 >> 14) & 3;
        const int w = kSizes[shape][size][0], h = kSizes[shape][size][1];
        const bool dbl = affine && (a0 & 0x200);
        const int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        int x = a1 & 0x1FF, y = a0 & 0xFF;
        if (x >= 256) x -= 512;
        if (y >= 192 && y >= 256 - bh) y -= 256;
        const bool c256 = a0 & 0x2000;
        const bool hflip = !affine && (a1 & 0x1000);
        const bool vflip = !affine && (a1 & 0x2000);
        const uint32_t tile = a2 & 0x3FF;
        const uint32_t pal = (a2 >> 12) & 0xF;
        if (bitmap && !pal) continue; // Alpha-OAM == 0 hides Bitmap OBJ
        const uint8_t objPriority = static_cast<uint8_t>((a2 >> 10) & 3);

        int pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            const int grp = (a1 >> 9) & 0x1F;
            pa = (int16_t)rd16(oam_base + (grp * 4 + 0) * 8u + 6);
            pb = (int16_t)rd16(oam_base + (grp * 4 + 1) * 8u + 6);
            pc = (int16_t)rd16(oam_base + (grp * 4 + 2) * 8u + 6);
            pd = (int16_t)rd16(oam_base + (grp * 4 + 3) * 8u + 6);
        }

        for (int sy = 0; sy < bh; ++sy) {
            const int py = y + sy;
            if (py < 0 || py >= SCREEN_H) continue;
            for (int sx = 0; sx < bw; ++sx) {
                const int px = x + sx;
                if (px < 0 || px >= SCREEN_W ||
                    objPriority > fb.priority[py][px]) continue;
                // texel coordinate: affine maps through the matrix around the
                // box center; plain is direct with optional flips.
                int tx, ty;
                if (affine) {
                    const int cx = sx - bw / 2, cy = sy - bh / 2;
                    tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                    ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                    if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                } else {
                    tx = hflip ? w - 1 - sx : sx;
                    ty = vflip ? h - 1 - sy : sy;
                }
                if (bitmap) {
                    // DS Bitmap OBJ uses 16-bit direct-color pixels and a
                    // separate 1D/2D address formula, not 4/8bpp OBJ tiles.
                    const bool oneD = dispcnt & (1u << 6);
                    const unsigned bitmapWidth = (dispcnt & (1u << 5)) ? 256u : 128u;
                    const unsigned maskX = bitmapWidth / 8u - 1u;
                    const uint32_t bitmapOffset = oneD
                        ? tile * (128u << ((dispcnt >> 22) & 1u))
                        : (tile & maskX) * 16u + (tile & ~maskX) * 128u;
                    const unsigned stride = oneD ? unsigned(w) : bitmapWidth;
                    const uint32_t offset = bitmapOffset + (unsigned(ty) * stride +
                                                             unsigned(tx)) * 2u;
                    // Each engine has a finite OBJ window; reject invalid
                    // extents instead of reading neighboring DS VRAM banks.
                    const uint32_t capacity = eng == ENGINE_A ? 0x40000u : 0x20000u;
                    if (offset + 2u > capacity) continue;
                    const uint16_t pixel = rd16(obj_vram + offset);
                    if (!(pixel & 0x8000u)) continue;
                    uint32_t color = bgr555(pixel);
                    // Master brightness was applied to the underlying BG
                    // already. Apply it to the OBJ before blending so a
                    // semi-transparent pixel does not fade the BG twice.
                    if (masterMode == 1 || masterMode == 2) {
                        uint32_t transformed = 0xff000000u;
                        for (unsigned shift = 0; shift < 24; shift += 8) {
                            unsigned component = (color >> shift) & 255u;
                            if (masterMode == 1)
                                component += ((255u - component) * masterLevel) / 16u;
                            else
                                component -= component * masterLevel / 16u;
                            transformed |= component << shift;
                        }
                        color = transformed;
                    }
                    const unsigned alpha = pal + 1u;
                    const unsigned underneath = fb.layer[py][px];
                    if (alpha < 16 && underneath <= 5 &&
                        (blendControl & (1u << (8u + underneath))))
                        color = blendColors(color, fb.px[py][px], alpha, 16u - alpha);
                    fb.px[py][px] = color;
                    fb.priority[py][px] = objPriority;
                    fb.layer[py][px] = 4;
                    continue;
                }
                const int tcol = tx >> 3, trow = ty >> 3;
                const int fx = tx & 7, fy = ty & 7;
                // 1D mapping: tiles of one sprite are consecutive.
                const int tiles_per_row = w / 8;
                const bool oneDimensional=dispcnt&(1u<<4);
                const uint32_t tno=trow*(oneDimensional?tiles_per_row:(c256?16:32))+tcol;
                const uint32_t baseTile=oneDimensional?tile*boundary:(c256?(tile&~1u):tile)*32u;
                uint32_t index;
                if (c256) {
                    const uint32_t addr = obj_vram + baseTile
                                          + tno * 64u + fy * 8u + fx;
                    index = rd8(addr);
                    if (!index) continue;
                    const unsigned below = fb.layer[py][px];
                    uint32_t color = colors[index];
                    if (objMode == 1 && below <= 5 &&
                        (blendControl & (1u << (8u + below))))
                        color = blendColors(color, fb.px[py][px], eva, evb);
                    fb.px[py][px] = color;
                    fb.priority[py][px] = objPriority;
                    fb.layer[py][px] = 4;
                } else {
                    const uint32_t addr = obj_vram + baseTile
                                          + tno * 32u + fy * 4u + fx / 2;
                    const uint8_t b = rd8(addr);
                    index = (fx & 1) ? (b >> 4) : (b & 0xF);
                    if (!index) continue;
                    const unsigned below = fb.layer[py][px];
                    uint32_t color = colors[pal * 16u + index];
                    if (objMode == 1 && below <= 5 &&
                        (blendControl & (1u << (8u + below))))
                        color = blendColors(color, fb.px[py][px], eva, evb);
                    fb.px[py][px] = color;
                    fb.priority[py][px] = objPriority;
                    fb.layer[py][px] = 4;
                }
            }
        }
    }
}

void ppu_capture(const Framebuffer &composited,const Framebuffer &geometry) {
    const uint32_t control=static_cast<uint32_t>(io_read(0x04000064,4));
    if (!(control&0x80000000u)) return;
    const unsigned bank=(control>>16)&3,mode=(control>>29)&3;
    if ((io_read(0x04000240+bank,1)&0x87)!=0x80) return;
    if (mode && (control&(1u<<25))) throw std::runtime_error("Display FIFO capture is not linked");
    constexpr unsigned widths[]={128,256,256,256},heights[]={128,64,128,192};
    const unsigned size=(control>>20)&3,width=widths[size],height=heights[size];
    const unsigned writeOffset=((control>>18)&3)*0x8000;
    const uint32_t display=static_cast<uint32_t>(io_read(0x04000000,4));
    const unsigned readBank=(display>>18)&3;
    const unsigned readOffset=((display>>16)&3)==2?0:((control>>26)&3)*0x8000;
    const unsigned eva=std::min(16u,control&31),evb=std::min(16u,(control>>8)&31);
    auto convert=[](uint32_t pixel) {
        return uint16_t(((pixel>>19)&31)|(((pixel>>11)&31)<<5)|(((pixel>>3)&31)<<10)|
                        ((pixel>>24)?0x8000:0));
    };
    std::array<uint16_t,256*192> sourceB{};
    if (mode) for (unsigned i=0;i<width*height;++i)
        sourceB[i]=rd16(0x06800000+readBank*0x20000+((readOffset+i*2)&0x1ffff));
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        const unsigned i=y*width+x;
        const uint16_t a=convert((control&(1u<<24))?geometry.px[y][x]:composited.px[y][x]);
        const uint16_t b=sourceB[i];
        uint16_t result=mode==1?b:a;
        if (mode>=2) {
            const unsigned aa=(a>>15)*eva,bb=(b>>15)*evb;
            result=aa || bb?0x8000:0;
            for (unsigned shift=0;shift<15;shift+=5)
                result|=std::min(31u,(((a>>shift)&31)*aa+((b>>shift)&31)*bb)/16)<<shift;
        }
        *static_cast<uint16_t *>(host_ptr(0x06800000u+bank*0x20000+((writeOffset+i*2)&0x1ffff)))=result;
    }
    io_write(0x04000064,control&~0x80000000u,4);
}

bool ppu_write_bmp(const char *path, const Framebuffer &fb) {
    std::FILE *f = std::fopen(path, "wb");
    if (!f) return false;

    const uint32_t stride = SCREEN_W * 3;
    const uint32_t pad = (4 - (stride & 3)) & 3;
    const uint32_t img = (stride + pad) * SCREEN_H;
    const uint32_t off = 54;

    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    const uint32_t total = off + img;
    std::memcpy(hdr + 2, &total, 4);
    std::memcpy(hdr + 10, &off, 4);
    const uint32_t dib = 40;
    std::memcpy(hdr + 14, &dib, 4);
    const int32_t w = SCREEN_W, h = SCREEN_H;
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &img, 4);
    std::fwrite(hdr, 1, sizeof hdr, f);

    const uint8_t zero[3] = {0, 0, 0};
    for (int y = SCREEN_H - 1; y >= 0; --y) {          // BMP rows run bottom-up
        for (int x = 0; x < SCREEN_W; ++x) {
            const uint32_t c = fb.px[y][x];
            const uint8_t bgr[3] = {static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8),
                                    static_cast<uint8_t>(c >> 16)};
            std::fwrite(bgr, 1, 3, f);
        }
        if (pad) std::fwrite(zero, 1, pad, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace ntr

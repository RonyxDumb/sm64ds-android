// The DS 2D engines in software: text, affine and bitmap backgrounds, sprites, windows, colour effects, master
// brightness, VRAM display and display capture. Renders from a FrameSnapshot, one scanline at a time, at the native
// 256x192 resolution.
#include "port.h"
#include "gpu2d.h"

#include <string.h>
#include <stdlib.h>

static void capture_done_in_process(void) {
    *(volatile uint8_t *) (uintptr_t) 0x04000067 &= 0x7f; // DISPCAPCNT: capture finished
}
void (*g_capture_done)(void) = capture_done_in_process;

// ------------------------------------------------------------------------------------------------ VRAM mapping
#define PAGE 0x4000u

static void map_pages(VramMap *m, int region, uint32_t region_offset, int bank, uint32_t size) {
    for (uint32_t off = 0; off < size; off += PAGE) {
        uint32_t page = (region_offset + off) / PAGE;
        if (page < 48) {
            m->page[region][page] = (int32_t) (g_vram_bank_offset[bank] + off);
        }
    }
}

void vram_map_build(VramMap *m, const uint8_t vramcnt[10]) {
    memset(m, 0xff, sizeof(*m));
    static const int8_t bank_of[10] = {VRAM_A, VRAM_B, VRAM_C, VRAM_D, VRAM_E, VRAM_F, VRAM_G, -1, VRAM_H, VRAM_I};
    static const uint32_t lcdc_offset[VRAM_BANK_COUNT] = {0x00000, 0x20000, 0x40000, 0x60000, 0x80000,
                                                          0x90000, 0x94000, 0x98000, 0xa0000};
    for (int i = 0; i < 10; i++) {
        int bank = bank_of[i];
        if (bank < 0) {
            continue;
        }
        uint8_t cnt = vramcnt[i];
        if (!(cnt & 0x80)) {
            continue;
        }
        uint32_t mst = cnt & 7, ofs = (cnt >> 3) & 3, size = g_vram_bank_size[bank];
        switch (bank) {
        case VRAM_A:
        case VRAM_B:
        case VRAM_C:
        case VRAM_D:
            if (bank >= VRAM_C && mst == 4) {
                map_pages(m, bank == VRAM_C ? VR_BG_B : VR_OBJ_B, 0, bank, size);
                break;
            }
            switch (mst & 3) {
            case 0: map_pages(m, VR_LCDC, lcdc_offset[bank], bank, size); break;
            case 1: map_pages(m, VR_BG_A, ofs * 0x20000, bank, size); break;
            case 2:
                if (bank <= VRAM_B) map_pages(m, VR_OBJ_A, (ofs & 1) * 0x20000, bank, size);
                break;
            case 3: map_pages(m, VR_TEX, ofs * 0x20000, bank, size); break;
            }
            break;
        case VRAM_E:
            switch (mst) {
            case 0: map_pages(m, VR_LCDC, lcdc_offset[bank], bank, size); break;
            case 1: map_pages(m, VR_BG_A, 0, bank, size); break;
            case 2: map_pages(m, VR_OBJ_A, 0, bank, size); break;
            case 3: map_pages(m, VR_TEXPAL, 0, bank, size); break;
            case 4: map_pages(m, VR_BGEXT_A, 0, bank, 0x8000); break;
            }
            break;
        case VRAM_F:
        case VRAM_G: {
            uint32_t o = (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000;
            switch (mst) {
            case 0: map_pages(m, VR_LCDC, lcdc_offset[bank], bank, size); break;
            case 1: map_pages(m, VR_BG_A, o, bank, size); break;
            case 2: map_pages(m, VR_OBJ_A, o, bank, size); break;
            case 3: map_pages(m, VR_TEXPAL, ((ofs & 1) + (ofs >> 1) * 4) * 0x4000, bank, size); break;
            case 4: map_pages(m, VR_BGEXT_A, (ofs & 1) * 0x4000, bank, size); break;
            case 5: map_pages(m, VR_OBJEXT_A, 0, bank, size); break;
            }
            break;
        }
        case VRAM_H:
            switch (mst & 3) {
            case 0: map_pages(m, VR_LCDC, lcdc_offset[bank], bank, size); break;
            case 1: map_pages(m, VR_BG_B, 0, bank, size); break;
            case 2: map_pages(m, VR_BGEXT_B, 0, bank, size); break;
            }
            break;
        case VRAM_I:
            switch (mst & 3) {
            case 0: map_pages(m, VR_LCDC, lcdc_offset[bank], bank, size); break;
            case 1: map_pages(m, VR_BG_B, 0x8000, bank, size); break;
            case 2: map_pages(m, VR_OBJ_B, 0, bank, size); break;
            case 3: map_pages(m, VR_OBJEXT_B, 0, bank, size); break;
            }
            break;
        }
    }
}

const uint8_t *vram_ptr(const VramMap *m, const uint8_t *vram, int region, uint32_t offset) {
    uint32_t page = offset / PAGE;
    if (page >= 48 || m->page[region][page] < 0) {
        return NULL;
    }
    return vram + m->page[region][page] + (offset % PAGE);
}

static inline uint8_t rd8(const VramMap *m, const uint8_t *vram, int region, uint32_t offset) {
    const uint8_t *p = vram_ptr(m, vram, region, offset);
    return p ? *p : 0;
}

static inline uint16_t rd16(const VramMap *m, const uint8_t *vram, int region, uint32_t offset) {
    const uint8_t *p = vram_ptr(m, vram, region, offset & ~1u);
    return p ? (uint16_t) (p[0] | p[1] << 8) : 0;
}

// ------------------------------------------------------------------------------------------------ engine state
// A layer pixel: BGR555 colour in bits 0..14, bit 15 = present. 3D pixels carry their alpha and the full colour.
typedef struct Engine {
    const FrameSnapshot *f;
    const VramMap *map;
    const uint8_t *io; // this engine's registers
    const uint16_t *pal; // BG palette (256), OBJ palette follows at +256
    bool is_a;
    int bg_region, obj_region, bgext_region, objext_region;
    uint32_t dispcnt;
    // Widescreen: DS columns drawn beyond each side (the line is 256 + 2 * margin wide, DS x = index - margin).
    // Only the bottom background layer and the backdrop continue there; sprites near the side edges move out with
    // the screen edges in races (hud_shift).
    int margin;
    bool hud_shift;
    int8_t obj_shift[128]; // hud_shift: -1 / 0 / 1 margin for each sprite
    int ext_bg; // the layer drawn across the whole width, -1 none
    // A still picture as the extended layer: beyond the DS screen it shows its edge colours, blurred vertically
    // (per line, BGR555 | 0x8000 when opaque), fading a little towards the outside, instead of repeating.
    const uint16_t *side[2];
} Engine;

static inline uint16_t fade555(uint16_t c, int d, int margin) {
    if (!(c & 0x8000)) return 0;
    int f = 256 - d * 96 / margin; // down to 62% at the outer edge
    int r = (c & 31) * f >> 8, g = ((c >> 5) & 31) * f >> 8, b = ((c >> 10) & 31) * f >> 8;
    return (uint16_t) (0x8000 | r | g << 5 | b << 10);
}

static inline uint16_t io16(const Engine *e, uint32_t off) { return (uint16_t) (e->io[off] | e->io[off + 1] << 8); }
static inline uint32_t io32(const Engine *e, uint32_t off) { return io16(e, off) | (uint32_t) io16(e, off + 2) << 16; }

#define LAYER_OBJ 4
#define LAYER_BD 5

typedef struct Line { // indexed by DS x + margin
    uint16_t bg[4][GPU2D_MAX_W];   // bit 15 = opaque
    uint16_t obj[GPU2D_MAX_W];
    uint8_t obj_prio[GPU2D_MAX_W]; // 4 = none
    uint8_t obj_alpha[GPU2D_MAX_W]; // 0xff normal, 0xfe semi-transparent, 0..15 bitmap alpha
    uint8_t obj_window[GPU2D_MAX_W];
} Line;

// Columns a background layer covers: the whole width for the extended layer, the DS screen otherwise.
static inline void bg_span(const Engine *e, int bg, int *x0, int *x1) {
    bool ext = bg == e->ext_bg;
    *x0 = ext ? -e->margin : 0;
    *x1 = ext ? 256 + e->margin : 256;
}

static uint16_t ext_palette_color(const Engine *e, int region, uint32_t slot_offset, uint32_t index) {
    return rd16(e->map, e->f->vram, region, slot_offset + index * 2) & 0x7fff;
}

// Text background.
static void draw_text_bg(const Engine *e, int bg, int y, uint16_t *out) {
    uint16_t cnt = io16(e, 0x08 + bg * 2);
    uint32_t hofs = io16(e, 0x10 + bg * 4) & 0x1ff, vofs = io16(e, 0x12 + bg * 4) & 0x1ff;
    uint32_t size = cnt >> 14;
    uint32_t w = size & 1 ? 512 : 256, h = size & 2 ? 512 : 256;
    uint32_t screen_base = ((cnt >> 8) & 31) * 0x800, char_base = ((cnt >> 2) & 15) * 0x4000;
    if (e->is_a) {
        screen_base += ((e->dispcnt >> 27) & 7) * 0x10000;
        char_base += ((e->dispcnt >> 24) & 7) * 0x10000;
    }
    bool c256 = cnt & 0x80;
    bool ext = c256 && (e->dispcnt & 0x40000000);
    uint32_t ext_slot = (uint32_t) bg;
    if (bg < 2 && (cnt & 0x2000)) {
        ext_slot += 2;
    }
    uint32_t sy = (y + vofs) & (h - 1);
    int x0, x1;
    bg_span(e, bg, &x0, &x1);
    out += e->margin;
    for (int x = x0; x < x1; x++) {
        if ((x < 0 || x > 255) && e->side[0]) {
            out[x] = fade555(e->side[x > 255][y], x < 0 ? -x : x - 255, e->margin);
            continue;
        }
        uint32_t sx = ((uint32_t) x + hofs) & (w - 1);
        uint32_t block = (sx >= 256 ? 1 : 0) + (sy >= 256 ? (w == 512 ? 2 : 1) : 0);
        uint32_t map_addr = screen_base + block * 0x800 + ((sy & 255) / 8) * 64 + ((sx & 255) / 8) * 2;
        uint16_t entry = rd16(e->map, e->f->vram, e->bg_region, map_addr);
        uint32_t tx = sx & 7, ty = sy & 7;
        if (entry & 0x400) tx = 7 - tx;
        if (entry & 0x800) ty = 7 - ty;
        uint32_t tile = entry & 0x3ff;
        uint16_t color;
        if (c256) {
            uint8_t idx = rd8(e->map, e->f->vram, e->bg_region, char_base + tile * 64 + ty * 8 + tx);
            if (!idx) continue;
            color = ext ? ext_palette_color(e, e->bgext_region, ext_slot * 0x2000 + (entry >> 12) * 512, idx)
                        : e->pal[idx];
        } else {
            uint8_t b = rd8(e->map, e->f->vram, e->bg_region, char_base + tile * 32 + ty * 4 + tx / 2);
            uint8_t idx = tx & 1 ? b >> 4 : b & 15;
            if (!idx) continue;
            color = e->pal[(entry >> 12) * 16 + idx];
        }
        out[x] = (color & 0x7fff) | 0x8000;
    }
}

// Affine and extended backgrounds (BG2/BG3).
static void draw_affine_bg(const Engine *e, int bg, int y, uint16_t *out, int kind) {
    // kind: 0 affine (8-bit map), 1 extended with 16-bit map, 2 256-colour bitmap, 3 direct colour bitmap
    uint16_t cnt = io16(e, 0x08 + bg * 2);
    uint32_t base = 0x20 + (bg - 2) * 0x10;
    int32_t pa = (int16_t) io16(e, base), pb = (int16_t) io16(e, base + 2);
    int32_t pc = (int16_t) io16(e, base + 4), pd = (int16_t) io16(e, base + 6);
    int32_t rx = ((int32_t) (io32(e, base + 8) << 4)) >> 4, ry = ((int32_t) (io32(e, base + 12) << 4)) >> 4;
    rx += pb * y;
    ry += pd * y;
    bool wrap = cnt & 0x2000;
    uint32_t size = cnt >> 14;
    uint32_t w, h;
    if (kind <= 1) {
        w = h = 128u << size;
    } else {
        static const uint16_t bw[4] = {128, 256, 512, 512}, bh[4] = {128, 256, 256, 512};
        w = bw[size];
        h = bh[size];
    }
    uint32_t screen_base = ((cnt >> 8) & 31) * 0x800, char_base = ((cnt >> 2) & 15) * 0x4000;
    if (e->is_a && kind <= 1) {
        screen_base += ((e->dispcnt >> 27) & 7) * 0x10000;
        char_base += ((e->dispcnt >> 24) & 7) * 0x10000;
    }
    uint32_t bitmap_base = ((cnt >> 8) & 31) * 0x4000;
    bool ext = e->dispcnt & 0x40000000;
    int x0, x1;
    bg_span(e, bg, &x0, &x1);
    out += e->margin;
    for (int x = x0; x < x1; x++) {
        if ((x < 0 || x > 255) && e->side[0]) {
            out[x] = fade555(e->side[x > 255][y], x < 0 ? -x : x - 255, e->margin);
            continue;
        }
        int32_t sx = (rx + pa * x) >> 8, sy = (ry + pc * x) >> 8;
        if (wrap) {
            sx &= (int32_t) w - 1;
            sy &= (int32_t) h - 1;
        } else if (sx < 0 || sy < 0 || sx >= (int32_t) w || sy >= (int32_t) h) {
            continue;
        }
        uint16_t color;
        if (kind == 0) {
            uint8_t tile = rd8(e->map, e->f->vram, e->bg_region, screen_base + (sy / 8) * (w / 8) + sx / 8);
            uint8_t idx = rd8(e->map, e->f->vram, e->bg_region, char_base + tile * 64 + (sy & 7) * 8 + (sx & 7));
            if (!idx) continue;
            color = e->pal[idx];
        } else if (kind == 1) {
            uint16_t entry = rd16(e->map, e->f->vram, e->bg_region, screen_base + ((sy / 8) * (w / 8) + sx / 8) * 2);
            uint32_t tx = sx & 7, ty = sy & 7;
            if (entry & 0x400) tx = 7 - tx;
            if (entry & 0x800) ty = 7 - ty;
            uint8_t idx = rd8(e->map, e->f->vram, e->bg_region, char_base + (entry & 0x3ff) * 64 + ty * 8 + tx);
            if (!idx) continue;
            color = ext ? ext_palette_color(e, e->bgext_region, bg * 0x2000 + (entry >> 12) * 512, idx) : e->pal[idx];
        } else if (kind == 2) {
            uint8_t idx = rd8(e->map, e->f->vram, e->bg_region, bitmap_base + sy * w + sx);
            if (!idx) continue;
            color = e->pal[idx];
        } else {
            uint16_t c = rd16(e->map, e->f->vram, e->bg_region, bitmap_base + (sy * w + sx) * 2);
            if (!(c & 0x8000)) continue;
            color = c;
        }
        out[x] = (color & 0x7fff) | 0x8000;
    }
}

// One line of a background layer, as the BG mode makes it.
static void draw_bg_line(const Engine *e, int bg, int y, uint16_t *out) {
    uint32_t mode = e->dispcnt & 7;
    if (bg < 2 || (bg == 2 && (mode == 0 || mode == 1 || mode == 3)) || (bg == 3 && mode == 0)) {
        draw_text_bg(e, bg, y, out);
    } else if ((bg == 2 && (mode == 2 || mode == 1)) || (bg == 3 && (mode == 1 || mode == 2))) {
        draw_affine_bg(e, bg, y, out, 0);
    } else {
        uint16_t cnt = io16(e, 0x08 + bg * 2);
        int kind = !(cnt & 0x80) ? 1 : (cnt & 4) ? 3 : 2;
        draw_affine_bg(e, bg, y, out, kind);
    }
}

// Sprites of one line.
static const uint8_t s_obj_w[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
static const uint8_t s_obj_h[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

// Race HUD in widescreen: sprites that touch each other form groups (a counter, a word); a group lying by a side of
// the DS screen moves out to that side of the wide screen, one across the middle (titles) stays.
static void plan_hud_shift(Engine *e) {
    const uint16_t *oam = (const uint16_t *) (e->f->oam + (e->is_a ? 0 : 0x400));
    int x0[128], x1[128], y0[128], y1[128], group[128];
    bool on[128];
    for (int i = 0; i < 128; i++) {
        const uint16_t *a = oam + i * 4;
        uint16_t a0 = a[0], a1 = a[1];
        bool affine = a0 & 0x100;
        uint32_t shape = (a0 >> 14) & 3, size = (a1 >> 14) & 3;
        on[i] = (affine || !(a0 & 0x200)) && shape != 3;
        group[i] = i;
        e->obj_shift[i] = 0;
        if (!on[i]) continue;
        int w = s_obj_w[shape][size], h = s_obj_h[shape][size];
        if (affine && (a0 & 0x200)) w *= 2, h *= 2;
        int x = a1 & 0x1ff, y = a0 & 0xff;
        if (x >= 256) x -= 512;
        if (y >= 192) y -= 256;
        x0[i] = x, x1[i] = x + w, y0[i] = y, y1[i] = y + h;
        if (x1[i] <= 0 || x0[i] >= 256 || y1[i] <= 0 || y0[i] >= 192) on[i] = false; // off screen
    }
    // Groups: overlapping vertically and at most a space apart horizontally (words of a title stay together).
    for (int i = 0; i < 128; i++) {
        if (!on[i]) continue;
        for (int j = i + 1; j < 128; j++) {
            if (!on[j]) continue;
            if (y0[i] >= y1[j] || y0[j] >= y1[i]) continue;
            if (x0[i] > x1[j] + 16 || x0[j] > x1[i] + 16) continue;
            int a = i, b = j;
            while (group[a] != a) a = group[a];
            while (group[b] != b) b = group[b];
            if (a != b) group[b < a ? a : b] = b < a ? b : a;
        }
    }
    int gx0[128], gx1[128];
    for (int i = 0; i < 128; i++) gx0[i] = 1000, gx1[i] = -1000;
    for (int i = 0; i < 128; i++) {
        if (!on[i]) continue;
        int g = i;
        while (group[g] != g) g = group[g];
        group[i] = g;
        if (x0[i] < gx0[g]) gx0[g] = x0[i];
        if (x1[i] > gx1[g]) gx1[g] = x1[i];
    }
    for (int i = 0; i < 128; i++) {
        if (!on[i]) continue;
        int g = group[i], w = gx1[g] - gx0[g], cx = (gx0[g] + gx1[g]) / 2;
        if (w > 128) continue; // spans the screen
        if (cx < 72) e->obj_shift[i] = -1;
        else if (cx > 184) e->obj_shift[i] = 1;
    }
}


static void draw_objs(const Engine *e, int y, Line *line) {
    const uint16_t *oam = (const uint16_t *) (e->f->oam + (e->is_a ? 0 : 0x400));
    bool map1d = e->dispcnt & 0x10;
    uint32_t bound = map1d ? 32u << ((e->dispcnt >> 20) & 3) : 32;
    bool ext = e->dispcnt & 0x80000000u;
    for (int i = 127; i >= 0; i--) { // backwards: lower OAM indices win
        const uint16_t *a = oam + i * 4;
        uint16_t a0 = a[0], a1 = a[1], a2 = a[2];
        bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue; // disabled
        uint32_t shape = (a0 >> 14) & 3, size = (a1 >> 14) & 3;
        if (shape == 3) continue;
        int w = s_obj_w[shape][size], h = s_obj_h[shape][size];
        int bw = w, bh = h; // bounding box
        if (affine && (a0 & 0x200)) {
            bw *= 2;
            bh *= 2;
        }
        int oy = a0 & 0xff;
        int dy = (y - oy) & 0xff;
        if (dy >= bh) continue;
        int ox = a1 & 0x1ff;
        if (ox >= 256) ox -= 512;
        if (e->hud_shift) ox += e->obj_shift[i] * e->margin;
        uint32_t mode = (a0 >> 10) & 3;
        uint32_t prio = (a2 >> 10) & 3;
        uint32_t tile = a2 & 0x3ff;
        bool c256 = a0 & 0x2000;
        int32_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
        if (affine) {
            const uint16_t *p = oam + ((a1 >> 9) & 31) * 16;
            pa = (int16_t) p[3];
            pb = (int16_t) p[7];
            pc = (int16_t) p[11];
            pd = (int16_t) p[15];
        }
        for (int bx = 0; bx < bw; bx++) {
            int x = ox + bx;
            if (x < -e->margin || x >= 256 + e->margin) continue;
            int lx = x + e->margin;
            int tx, ty;
            if (affine) {
                int cx = bx - bw / 2, cy = dy - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = a1 & 0x1000 ? w - 1 - bx : bx;
                ty = a1 & 0x2000 ? h - 1 - dy : dy;
            }
            uint16_t color;
            uint8_t alpha = mode == 1 ? 0xfe : 0xff;
            if (mode == 3) { // bitmap OBJ
                uint32_t addr;
                if (e->dispcnt & 0x40) {
                    addr = tile * (128u << ((e->dispcnt >> 22) & 1)) + (ty * w + tx) * 2;
                } else {
                    uint32_t width = e->dispcnt & 0x20 ? 256 : 128;
                    uint32_t mask = width / 8 - 1;
                    addr = (((tile / (width / 8)) * 8 + ty) * width + (tile & mask) * 8 + tx) * 2;
                }
                uint16_t c = rd16(e->map, e->f->vram, e->obj_region, addr);
                if (!(c & 0x8000)) continue;
                color = c;
                alpha = (uint8_t) (a2 >> 12);
            } else {
                uint32_t addr;
                if (c256) {
                    uint32_t tbase = map1d ? tile * bound : tile * 32;
                    uint32_t cell = map1d ? (ty / 8) * (w / 8) + tx / 8 : (ty / 8) * 16 + tx / 8;
                    addr = map1d ? tbase + cell * 64 : tile * 32 + ((ty / 8) * 32 * 32) + (tx / 8) * 64;
                    addr += (ty & 7) * 8 + (tx & 7);
                    uint8_t idx = rd8(e->map, e->f->vram, e->obj_region, addr);
                    if (!idx) continue;
                    color = ext ? ext_palette_color(e, e->objext_region, (a2 >> 12) * 512, idx) : e->pal[256 + idx];
                } else {
                    uint32_t cell = map1d ? (ty / 8) * (w / 8) + tx / 8 : (ty / 8) * 32 + tx / 8;
                    addr = (map1d ? tile * bound : tile * 32) + cell * 32 + (ty & 7) * 4 + (tx & 7) / 2;
                    uint8_t b = rd8(e->map, e->f->vram, e->obj_region, addr);
                    uint8_t idx = tx & 1 ? b >> 4 : b & 15;
                    if (!idx) continue;
                    color = e->pal[256 + (a2 >> 12) * 16 + idx];
                }
            }
            if (mode == 2) {
                line->obj_window[lx] = 1;
                continue;
            }
            if (prio <= line->obj_prio[lx]) {
                line->obj[lx] = (color & 0x7fff) | 0x8000;
                line->obj_prio[lx] = (uint8_t) prio;
                line->obj_alpha[lx] = alpha;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ composition
static inline uint32_t rgb555_to_888(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | 0xff000000u;
}

// Blend in 8-bit per channel: (a * ea + b * eb) / 16, clamped.
static uint32_t blend(uint32_t a, uint32_t b, uint32_t ea, uint32_t eb) {
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        uint32_t v = (((a >> s) & 255) * ea + ((b >> s) & 255) * eb) / 16;
        out |= (v > 255 ? 255 : v) << s;
    }
    return out;
}

static uint32_t brighten(uint32_t c, uint32_t ey) {
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        uint32_t v = (c >> s) & 255;
        v += (255 - v) * ey / 16;
        out |= v << s;
    }
    return out;
}

static uint32_t darken(uint32_t c, uint32_t ey) {
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        uint32_t v = (c >> s) & 255;
        v -= v * ey / 16;
        out |= v << s;
    }
    return out;
}

// Window mask of each pixel: bits 0..5 = layers and effects enabled there.
static void windows(const Engine *e, int y, const Line *line, uint8_t *mask) {
    uint32_t wins = (e->dispcnt >> 13) & 7;
    int width = 256 + 2 * e->margin;
    if (!wins) {
        memset(mask, 0x3f, (size_t) width);
        return;
    }
    uint16_t winin = io16(e, 0x48), winout = io16(e, 0x4a);
    for (int i = 0; i < width; i++) {
        mask[i] = winout & 0x3f;
    }
    if (wins & 4) {
        for (int i = 0; i < width; i++) {
            if (line->obj_window[i]) mask[i] = (winout >> 8) & 0x3f;
        }
    }
    for (int w = 1; w >= 0; w--) {
        if (!(wins & (1u << w))) continue;
        uint16_t hreg = io16(e, 0x40 + w * 2), vreg = io16(e, 0x44 + w * 2);
        int x1 = hreg >> 8, x2 = hreg & 0xff, y1 = vreg >> 8, y2 = vreg & 0xff;
        bool in_y = y1 <= y2 ? (y >= y1 && y < y2) : (y >= y1 || y < y2);
        if (!in_y) continue;
        for (int i = 0; i < width; i++) {
            // Beyond the DS screen: as its edge columns (254 on the right: a window ending at 255 covers it).
            int x = i - e->margin;
            x = x < 0 ? 0 : x > 254 ? 254 : x;
            bool in_x = x1 <= x2 ? (x >= x1 && x < x2) : (x >= x1 || x < x2);
            if (in_x) mask[i] = (winin >> (w * 8)) & 0x3f;
        }
    }
}

// Split mode (layer3d == GPU2D_SPLIT): the 3D layer is drawn later at high resolution. `out` then receives what lies
// above the 3D layer (alpha = its opacity over what is below), `below` what lies under it (alpha 0: the 3D pixel
// replaces it; 255: it is a blend target, translucent 3D mixes with it; 1..254: also the weight of an opaque 3D pixel
// in an alpha blend).
static void render_engine_line(const Engine *e, int y, const uint32_t *layer3d, uint32_t *out, uint32_t *below) {
    Line line;
    memset(line.bg, 0, sizeof(line.bg));
    memset(line.obj, 0, sizeof(line.obj));
    memset(line.obj_prio, 4, sizeof(line.obj_prio));
    memset(line.obj_window, 0, sizeof(line.obj_window));
    uint32_t mode = e->dispcnt & 7;
    bool bg0_3d = e->is_a && (e->dispcnt & 8);
    for (int bg = 0; bg < 4; bg++) {
        if (!(e->dispcnt & (0x100u << bg))) continue;
        if (bg == 0 && bg0_3d) continue;
        draw_bg_line(e, bg, y, line.bg[bg]);
    }
    (void) mode;
    if (e->dispcnt & 0x1000) {
        draw_objs(e, y, &line);
    }
    uint8_t mask[GPU2D_MAX_W];
    windows(e, y, &line, mask);
    int width = 256 + 2 * e->margin;

    uint16_t bldcnt = io16(e, 0x50), bldalpha = io16(e, 0x52);
    uint32_t effect = (bldcnt >> 6) & 3;
    uint32_t eva = bldalpha & 31, evb = (bldalpha >> 8) & 31, evy = io16(e, 0x54) & 31;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;
    uint32_t backdrop = rgb555_to_888(e->pal[0]);
    uint32_t prio[4];
    for (int bg = 0; bg < 4; bg++) {
        prio[bg] = io16(e, 0x08 + bg * 2) & 3;
    }
    if (bg0_3d) {
        prio[0] = io16(e, 0x08) & 3;
    }

    if (layer3d == GPU2D_SPLIT && bg0_3d) {
        for (int x = 0; x < width; x++) {
            // The three topmost layers, with the 3D layer always present.
            int top[3] = {LAYER_BD, LAYER_BD, LAYER_BD};
            uint32_t color[3] = {backdrop, backdrop, backdrop};
            int found = 0;
            for (uint32_t p = 0; p < 4 && found < 3; p++) {
                if (line.obj_prio[x] == p && (mask[x] & 0x10) && (line.obj[x] & 0x8000)) {
                    top[found] = LAYER_OBJ;
                    color[found++] = rgb555_to_888(line.obj[x]);
                }
                for (int bg = 0; bg < 4 && found < 3; bg++) {
                    if (prio[bg] != p || !(mask[x] & (1u << bg)) || !(e->dispcnt & (0x100u << bg))) continue;
                    if (bg == 0) {
                        top[found] = 0;
                        color[found++] = 0;
                    } else if (line.bg[bg][x] & 0x8000) {
                        top[found] = bg;
                        color[found++] = rgb555_to_888(line.bg[bg][x]);
                    }
                }
            }
            int i3 = top[0] == 0 ? 0 : top[1] == 0 ? 1 : top[2] == 0 ? 2 : 3;
            bool effects = mask[x] & 0x20;
            uint32_t up = 0, down = backdrop & 0xffffff;
            if (i3 == 0) {
                // 3D on top: what is under it, and how they combine.
                down = color[1] & 0xffffff;
                bool second = (bldcnt >> (8 + top[1])) & 1;
                uint32_t w = 0;
                if (second) {
                    w = 255;
                    if (effects && (bldcnt & 1) && effect == 1) w = eva * 255 / 16 ? eva * 255 / 16 : 1;
                    if (w > 255) w = 255;
                }
                if (effects && (bldcnt & 1) && (effect == 2 || effect == 3)) {
                    up = (effect == 2 ? 0xffffffu : 0) | (evy * 255 / 16) << 24; // brighten / darken over 3D
                }
                below[x] = down | w << 24;
            } else {
                uint32_t c = color[0];
                bool first = (bldcnt >> top[0]) & 1;
                bool second = (bldcnt >> (8 + top[1])) & 1;
                bool semi = top[0] == LAYER_OBJ && line.obj_alpha[x] != 0xff;
                uint32_t a = 255;
                if ((semi || (effects && first && effect == 1)) && second) {
                    uint32_t ea = semi && line.obj_alpha[x] != 0xfe ? line.obj_alpha[x] + 1u : eva;
                    uint32_t eb = semi && line.obj_alpha[x] != 0xfe ? 16 - ea : evb;
                    if (top[1] == 0) {
                        a = ea * 255 / 16; // over the 3D layer: the shader blends
                        below[x] = (color[2] & 0xffffff) | (((bldcnt >> (8 + top[2])) & 1) ? 255u << 24 : 0);
                    } else {
                        c = blend(color[0], color[1], ea, eb);
                    }
                } else if (effects && first && effect == 2) {
                    c = brighten(c, evy);
                } else if (effects && first && effect == 3) {
                    c = darken(c, evy);
                }
                up = (c & 0xffffff) | a << 24;
                if (a == 255) below[x] = 0;
            }
            out[x] = up;
        }
        return;
    }

    for (int x = 0; x < width; x++) {
        // Find the two topmost layers.
        int top[2] = {LAYER_BD, LAYER_BD};
        uint32_t color[2] = {backdrop, backdrop};
        uint32_t alpha3d = 31;
        int found = 0;
        for (uint32_t p = 0; p < 4 && found < 2; p++) {
            if (line.obj_prio[x] == p && (mask[x] & 0x10) && (line.obj[x] & 0x8000)) {
                top[found] = LAYER_OBJ;
                color[found] = rgb555_to_888(line.obj[x]);
                found++;
                if (found == 2) break;
            }
            for (int bg = 0; bg < 4 && found < 2; bg++) {
                if (prio[bg] != p || !(mask[x] & (1u << bg)) || !(e->dispcnt & (0x100u << bg))) continue;
                if (bg == 0 && bg0_3d) {
                    int dx = x - e->margin;
                    if (!layer3d || dx < 0 || dx > 255) continue;
                    uint32_t c = layer3d[y * 256 + dx];
                    uint32_t a = c >> 24;
                    if (a == 0) continue;
                    if (found == 0) alpha3d = a;
                    top[found] = 0;
                    color[found] = (c & 0xffffff) | 0xff000000u;
                    found++;
                } else if (line.bg[bg][x] & 0x8000) {
                    top[found] = bg;
                    color[found] = rgb555_to_888(line.bg[bg][x]);
                    found++;
                }
            }
        }
        uint32_t c = color[0];
        bool effects = mask[x] & 0x20;
        bool first = (bldcnt >> top[0]) & 1;
        bool second = (bldcnt >> (8 + top[1])) & 1;
        if (top[0] == LAYER_OBJ && line.obj_alpha[x] != 0xff && second) {
            // Semi-transparent and bitmap sprites blend whatever the effect setting.
            uint32_t a = line.obj_alpha[x] == 0xfe ? eva : line.obj_alpha[x] + 1;
            uint32_t b = line.obj_alpha[x] == 0xfe ? evb : 16 - (line.obj_alpha[x] + 1);
            c = blend(color[0], color[1], a, b);
        } else if (top[0] == 0 && bg0_3d && alpha3d < 31 && second) {
            // Translucent 3D pixels blend with the layer below using their own alpha.
            c = blend(color[0], color[1], (alpha3d + 1) / 2, 16 - (alpha3d + 1) / 2);
        } else if (effects && first) {
            if (effect == 1 && second) {
                c = blend(color[0], color[1], eva, evb);
            } else if (effect == 2) {
                c = brighten(c, evy);
            } else if (effect == 3) {
                c = darken(c, evy);
            }
        }
        out[x] = c;
    }
}

static void master_brightness(uint16_t reg, uint32_t *line, int width) {
    uint32_t mode = reg >> 14, factor = reg & 31;
    if (factor > 16) factor = 16;
    if (!factor || mode == 0 || mode == 3) return;
    for (int x = 0; x < width; x++) {
        line[x] = mode == 1 ? brighten(line[x], factor) : darken(line[x], factor);
    }
}

// The layer seen behind all others (the one to continue beyond the DS screen): 2D BG number, 4 when it is the 3D
// layer, -1 when none.
static int bottom_layer(const Engine *e) {
    int best = -1, best_prio = -1;
    for (int bg = 0; bg < 4; bg++) {
        if (!(e->dispcnt & (0x100u << bg))) continue;
        int prio = io16(e, 0x08 + bg * 2) & 3;
        if (prio >= best_prio) { // the higher number is behind at equal priority
            best_prio = prio;
            best = bg == 0 && e->is_a && (e->dispcnt & 8) ? 4 : bg;
        }
    }
    return best;
}

static void render_engine(const FrameSnapshot *f, const VramMap *map, bool is_a, const uint32_t *layer3d,
                          uint32_t *out, uint32_t *below, int margin) {
    Engine e;
    e.f = f;
    e.map = map;
    e.is_a = is_a;
    e.io = f->io + (is_a ? 0 : 0x1000);
    e.pal = (const uint16_t *) (f->palette + (is_a ? 0 : 0x400));
    e.bg_region = is_a ? VR_BG_A : VR_BG_B;
    e.obj_region = is_a ? VR_OBJ_A : VR_OBJ_B;
    e.bgext_region = is_a ? VR_BGEXT_A : VR_BGEXT_B;
    e.objext_region = is_a ? VR_OBJEXT_A : VR_OBJEXT_B;
    e.dispcnt = io32(&e, 0);
    e.margin = margin;
    e.hud_shift = margin > 0 && is_a && f->scene == 2 && (e.dispcnt & 8);
    if (e.hud_shift) plan_hud_shift(&e);
    e.ext_bg = margin > 0 ? bottom_layer(&e) : -1;
    if (e.ext_bg == 4) e.ext_bg = -1;
    e.side[0] = e.side[1] = NULL;
    if (e.ext_bg >= 0) {
        // A scrolling layer or one with more map beyond the screen goes on as its map does (patterns, skies); a
        // still picture made for the screen (logos, title, menu backgrounds) is mirrored at its edges instead of
        // repeating.
        static uint16_t last_hofs[2][4];
        uint16_t cnt = io16(&e, 0x08 + e.ext_bg * 2), hofs = io16(&e, 0x10 + e.ext_bg * 4);
        bool text = e.ext_bg < 2 || (e.dispcnt & 7) == 0 || ((e.dispcnt & 7) == 1 && e.ext_bg == 2) ||
                    ((e.dispcnt & 7) == 3 && e.ext_bg == 2);
        bool scrolling = hofs != last_hofs[is_a ? 0 : 1][e.ext_bg];
        last_hofs[is_a ? 0 : 1][e.ext_bg] = hofs;
        bool more_map = text ? (cnt & 0x4000) != 0 : (cnt & 0x2000) != 0; // 512 wide / affine wrap-around
        if (!(scrolling || more_map)) {
            // Its edge columns, blurred vertically.
            static uint16_t edge[2][192], side[2][192];
            static uint16_t row[GPU2D_MAX_W];
            int bg = e.ext_bg;
            e.ext_bg = -1; // the DS columns only
            for (int y = 0; y < 192; y++) {
                memset(row, 0, sizeof(row));
                draw_bg_line(&e, bg, y, row);
                edge[0][y] = row[margin];
                edge[1][y] = row[margin + 255];
            }
            e.ext_bg = bg;
            for (int k = 0; k < 2; k++) {
                for (int y = 0; y < 192; y++) {
                    int sum[3] = {0, 0, 0}, n = 0, opaque = 0;
                    for (int j = y - 10; j <= y + 10; j++) {
                        if (j < 0 || j > 191) continue;
                        uint16_t c = edge[k][j];
                        n++;
                        if (!(c & 0x8000)) continue;
                        opaque++;
                        sum[0] += c & 31;
                        sum[1] += (c >> 5) & 31;
                        sum[2] += (c >> 10) & 31;
                    }
                    side[k][y] = opaque * 2 < n ? 0
                                                : (uint16_t) (0x8000 | sum[0] / opaque | (sum[1] / opaque) << 5 |
                                                              (sum[2] / opaque) << 10);
                }
            }
            e.side[0] = side[0];
            e.side[1] = side[1];
        }
    }
    int width = 256 + 2 * margin;
    uint32_t display = (e.dispcnt >> 16) & 3;
    if (!is_a && display > 1) display = 1;
    bool forced_blank = e.dispcnt & 0x80;

    static uint32_t engine_a_out[256 * 192];
    for (int y = 0; y < 192; y++) {
        uint32_t *line = out + y * width;
        if (forced_blank || display == 0) {
            for (int x = 0; x < width; x++) line[x] = 0xffffffffu; // white
            if (below) memset(below + y * width, 0, (size_t) width * 4);
            continue;
        }
        if (display == 2) { // VRAM display: an LCDC bank as a 256x192 direct-colour picture
            uint32_t bank = (e.dispcnt >> 18) & 3;
            for (int x = 0; x < width; x++) {
                int dx = x - margin;
                line[x] = dx < 0 || dx > 255 ? 0xff000000u
                                             : rgb555_to_888(rd16(map, f->vram, VR_LCDC,
                                                                  bank * 0x20000 + (y * 256 + dx) * 2));
            }
        } else if (display == 3) {
            for (int x = 0; x < width; x++) line[x] = 0xff000000u;
        } else {
            render_engine_line(&e, y, layer3d, line, below ? below + y * width : NULL);
        }
        if (is_a) {
            memcpy(engine_a_out + y * 256, line + margin, 256 * 4);
        }
        if (layer3d == GPU2D_SPLIT && is_a) {
            // The compositing shader applies the master brightness to the whole screen.
            if (!(e.dispcnt & 8) || display != 1 || forced_blank) {
                for (int x = 0; x < width; x++) below[y * width + x] = 0; // no 3D on this line
                for (int x = 0; x < width; x++) line[x] |= 0xff000000u;
            }
        } else {
            master_brightness(io16(&e, 0x6c), line, width);
        }
    }
    if (is_a && (f->io[0x67] & 0x80) && layer3d != GPU2D_SPLIT) {
        gpu2d_capture(f, engine_a_out, layer3d);
    }
}

void gpu2d_render(const FrameSnapshot *f, const uint32_t *layer3d, uint32_t *top, uint32_t *bottom, int top_margin) {
    VramMap map;
    vram_map_build(&map, f->vramcnt);
    uint16_t powcnt = (uint16_t) (f->io[0x304] | f->io[0x305] << 8);
    bool a_on_top = powcnt & 0x8000;
    render_engine(f, &map, true, layer3d, a_on_top ? top : bottom, NULL, a_on_top ? top_margin : 0);
    render_engine(f, &map, false, NULL, a_on_top ? bottom : top, NULL, a_on_top ? 0 : top_margin);
}

void gpu2d_render_split(const FrameSnapshot *f, uint32_t *engine_a_above, uint32_t *engine_a_below,
                        uint32_t *engine_b, int margin_a, int margin_b) {
    VramMap map;
    vram_map_build(&map, f->vramcnt);
    render_engine(f, &map, true, GPU2D_SPLIT, engine_a_above, engine_a_below, margin_a);
    render_engine(f, &map, false, NULL, engine_b, NULL, margin_b);
}

// ------------------------------------------------------------------------------------------------ capture
static inline uint16_t to555(uint32_t c) {
    return (uint16_t) (((c >> 3) & 31) | ((c >> 11) & 31) << 5 | ((c >> 19) & 31) << 10);
}

void gpu2d_capture(const FrameSnapshot *f, const uint32_t *engineA, const uint32_t *layer3d) {
    uint32_t cnt = (uint32_t) f->io[0x64] | f->io[0x65] << 8 | f->io[0x66] << 16 | (uint32_t) f->io[0x67] << 24;
    uint32_t eva = cnt & 31, evb = (cnt >> 8) & 31;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    uint32_t bank = (cnt >> 16) & 3, write_ofs = ((cnt >> 18) & 3) * 0x8000;
    static const uint16_t cw[4] = {128, 256, 256, 256}, ch[4] = {128, 64, 128, 192};
    uint32_t w = cw[(cnt >> 20) & 3], h = ch[(cnt >> 20) & 3];
    bool src_3d = cnt & (1u << 24);
    uint32_t source = (cnt >> 29) & 3;
    uint32_t dispcnt = (uint32_t) f->io[0] | f->io[1] << 8 | f->io[2] << 16 | (uint32_t) f->io[3] << 24;
    uint32_t read_bank = (dispcnt >> 18) & 3, read_ofs = ((cnt >> 26) & 3) * 0x8000;
    uint8_t *dst = g_vram_banks + g_vram_bank_offset[bank];
    const uint8_t *srcb = f->vram + g_vram_bank_offset[read_bank];
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t a_rgba = src_3d ? (layer3d ? layer3d[y * 256 + x] : 0) : engineA[y * 256 + x];
            uint16_t a = to555(a_rgba);
            bool a_alpha = src_3d ? (a_rgba >> 24) != 0 : true;
            uint32_t roff = (read_ofs + (y * 256 + x) * 2) & 0x1ffff;
            uint16_t b = (uint16_t) (srcb[roff] | srcb[roff + 1] << 8);
            uint16_t out;
            if (source == 0) {
                out = a | (a_alpha ? 0x8000 : 0);
            } else if (source == 1) {
                out = b;
            } else {
                uint32_t r = 0;
                for (int s = 0; s < 15; s += 5) {
                    uint32_t ca = a_alpha ? (a >> s) & 31 : 0, cb = b & 0x8000 ? (b >> s) & 31 : 0;
                    uint32_t v = (ca * eva + cb * evb + 8) / 16;
                    r |= (v > 31 ? 31 : v) << s;
                }
                out = (uint16_t) (r | ((a_alpha && eva) || ((b & 0x8000) && evb) ? 0x8000 : 0));
            }
            uint32_t woff = (write_ofs + (y * 256 + x) * 2) & 0x1ffff;
            dst[woff] = (uint8_t) out;
            dst[woff + 1] = (uint8_t) (out >> 8);
        }
    }
    g_capture_done();
}

bool gpu2d_engine_a_on_top(const FrameSnapshot *f) { return (f->io[0x305] & 0x80) != 0; }

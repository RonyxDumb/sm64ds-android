// C versions of the functions the decomp keeps in ARM assembly (tools/portify.py removes them), and of the few game
// functions that drive the hardware in a way the platform layer has to see.
#include "port.h"

#include <string.h>

#include "types.h"
#include "registers.h"
#include "model_material_set.h"
#include "matrix44_concat.h"
#include "char_canvas_text_draw.h"
#include "digest_hash_block.h"
#include "cpu_copy_fill.h"
#include "Main/Graphics/stem_sets.h"
#include "Main/Graphics/timed_quad.h"
#include "Main/Race/race_hud_keyboard_map_object_work.h"

#define FX_ONE 0x1000

// ------------------------------------------------------------------------------------------------ matrices
static void set(s32 *m, int n, const s32 *v) { memcpy(m, v, sizeof(s32) * n); }

void MTX_Identity22_(MtxFx22 *dst) { set((s32 *) dst, 4, (const s32[]) {FX_ONE, 0, 0, FX_ONE}); }
void MTX_Identity33_(MtxFx33 *dst) { set((s32 *) dst, 9, (const s32[]) {FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0, FX_ONE}); }
void MTX_Identity43_(MtxFx43 *dst) {
    set((s32 *) dst, 12, (const s32[]) {FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0, FX_ONE, 0, 0, 0});
}
void MTX_Identity44_(MtxFx44 *dst) {
    set((s32 *) dst, 16, (const s32[]) {FX_ONE, 0, 0, 0, 0, FX_ONE, 0, 0, 0, 0, FX_ONE, 0, 0, 0, 0, FX_ONE});
}
void MTX_Copy33To43_(const MtxFx33 *src, MtxFx43 *dst) {
    s32 *d = (s32 *) dst;
    memmove(d, src, 9 * 4);
    d[9] = d[10] = d[11] = 0;
}
void MTX_Copy43To44_(const MtxFx43 *src, MtxFx44 *dst) {
    const s32 *s = (const s32 *) src;
    s32 tmp[16];
    for (int r = 0; r < 4; r++) {
        tmp[r * 4 + 0] = s[r * 3 + 0];
        tmp[r * 4 + 1] = s[r * 3 + 1];
        tmp[r * 4 + 2] = s[r * 3 + 2];
        tmp[r * 4 + 3] = r == 3 ? FX_ONE : 0;
    }
    memcpy(dst, tmp, sizeof(tmp));
}
void MTX_Copy44To43_(const MtxFx44 *src, MtxFx43 *dst) {
    const s32 *s = (const s32 *) src;
    s32 tmp[12];
    for (int r = 0; r < 4; r++) {
        tmp[r * 3 + 0] = s[r * 4 + 0];
        tmp[r * 3 + 1] = s[r * 4 + 1];
        tmp[r * 3 + 2] = s[r * 4 + 2];
    }
    memcpy(dst, tmp, sizeof(tmp));
}
void MTX_Transpose33_(const void *src, void *dst) {
    const s32 *s = src;
    s32 t[9] = {s[0], s[3], s[6], s[1], s[4], s[7], s[2], s[5], s[8]};
    memcpy(dst, t, sizeof(t));
}
void MTX_Scale43_(MtxFx43 *dst, s32 x, s32 y, s32 z) {
    set((s32 *) dst, 12, (const s32[]) {x, 0, 0, 0, y, 0, 0, 0, z, 0, 0, 0});
}
void MTX_RotX33_(MtxFx33 *dst, s32 s, s32 c) { set((s32 *) dst, 9, (const s32[]) {FX_ONE, 0, 0, 0, c, s, 0, -s, c}); }
void MTX_RotY33_(MtxFx33 *dst, s32 s, s32 c) { set((s32 *) dst, 9, (const s32[]) {c, 0, -s, 0, FX_ONE, 0, s, 0, c}); }
void MTX_RotZ33_(MtxFx33 *dst, s32 s, s32 c) { set((s32 *) dst, 9, (const s32[]) {c, s, 0, -s, c, 0, 0, 0, FX_ONE}); }
void MTX_RotX43_(MtxFx43 *dst, s32 s, s32 c) {
    set((s32 *) dst, 12, (const s32[]) {FX_ONE, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0});
}
void MTX_RotY43_(MtxFx43 *dst, s32 s, s32 c) {
    set((s32 *) dst, 12, (const s32[]) {c, 0, -s, 0, FX_ONE, 0, s, 0, c, 0, 0, 0});
}
void MTX_RotZ43_(MtxFx43 *dst, s32 s, s32 c) {
    set((s32 *) dst, 12, (const s32[]) {c, s, 0, -s, c, 0, 0, 0, FX_ONE, 0, 0, 0});
}

// ------------------------------------------------------------------------------------------------ geometry FIFO
void GX_SendFifo48B(const void *pSrc, void *pDest) { MIi_CpuSend32(pSrc, pDest, 48); }
void GX_SendFifo64B(const void *pSrc, void *pDest) { MIi_CpuSend32(pSrc, pDest, 64); }
// 128 zero words: NOPs that flush a partly written packed command.
void GXi_NopClearFifo128_(void *dest) {
    static const u32 zeros[128];
    MIi_CpuSend32(zeros, dest, sizeof(zeros));
}

// The fading full-screen quad (src/Main/Graphics/stem_sets.c).
void DrawTimedQuad(TimedQuad *timedQuad) {
    PORT_IO_WRITE(reg_G3_POLYGON_ATTR, (0x090000c0u) | timedQuad->mAlpha << 0x10);
    PORT_IO_WRITE(reg_G3_MTX_SCALE, 0x8c000);
    PORT_IO_WRITE(reg_G3_MTX_SCALE, 0x6e000);
    PORT_IO_WRITE(reg_G3_MTX_SCALE, 0xa000);
    PORT_IO_WRITE(reg_G3_BEGIN_VTXS, 1);
    PORT_IO_WRITE(reg_G3_COLOR, timedQuad->mColor);
    WriteQuadVertex(0);
    WriteQuadVertex(1);
    WriteQuadVertex(2);
    WriteQuadVertex(3);
    PORT_IO_WRITE(reg_G3_END_VTXS, 0);
}

// ------------------------------------------------------------------------------------------------ bit counting
u32 OsCountZeroBits(u32 bitmap) { return bitmap ? (u32) __builtin_clz(bitmap) : 32; }
u32 WcmCountLeadingZero(u32 value) { return value ? (u32) __builtin_clz(value) : 32; }
u32 WcmCountBits(u32 bits) { return (u32) __builtin_popcount(bits); }

// ------------------------------------------------------------------------------------------------ text canvas
// Character index of the 8x8 cell (cx, cy) in a canvas made of OBJs in 1D mapping: the canvas is split into the
// largest OBJs (sizes from `objs`), row bands first, then columns.
u32 GetCharIndex1D(u32 cx, u32 cy, u32 areaWidth, u32 areaHeight, u32 objWidth, u32 objHeight) {
    u32 base = 0;
    u32 sw = objWidth, sh = objHeight; // log2 of the OBJ size in cells
    for (;;) {
        u32 fullH = areaHeight & (~0u << sh);
        u32 fullW = areaWidth & (~0u << sw);
        if (cy >= fullH) {
            // Below the band of full-height OBJs.
            base += areaWidth * fullH;
            if (cx < fullW) {
                areaWidth = fullW;
                cy -= fullH;
                areaHeight -= fullH;
            } else {
                areaHeight -= fullH;
                base += fullW * areaHeight;
                cx -= fullW;
                cy -= fullH;
                areaWidth -= fullW;
            }
        } else if (cx >= fullW) {
            // Right of the full-width OBJs, inside the band.
            base += fullW * fullH;
            areaHeight = fullH;
            cx -= fullW;
            areaWidth -= fullW;
        } else {
            u32 hiMask = ~0u << sh, wMask = ~0u << sw;
            u32 index = fullW * (cy & hiMask) + base;
            index += (cx & wMask) << sh;
            index += (cy & ~hiMask) << sw;
            return index + (cx & ~wMask);
        }
        u32 lw = areaWidth >= 8 ? 3 : 31 - (u32) __builtin_clz(areaWidth);
        u32 lh = areaHeight >= 8 ? 3 : 31 - (u32) __builtin_clz(areaHeight);
        sw = objs[lh][lw][0];
        sh = objs[lh][lw][1];
    }
}

void DrawGlyph1D(NNSG2dCharCanvas *pCanvas, NNSG2dFont *pFont, s32 drawX, s32 drawY, u32 cl, NNSG2dGlyph *pGlyph);
void ClearContinuous(NNSG2dCharCanvas *pCanvas, u32 cl);
void ClearArea1D(NNSG2dCharCanvas *pCanvas, u32 cl, s32 areaX, s32 areaY, s32 areaWidth, s32 areaHeight);

void NNS_G2dCharCanvasInitForOBJ1D(void *pCC, void *charBase, s32 areaWidth, s32 areaHeight, s32 colorMode) {
    // An empty canvas (a label with no text, width 0) gets the shape of the smallest OBJ.
    u32 lw = areaWidth >= 8 ? 3 : areaWidth < 1 ? 0 : 31 - (u32) __builtin_clz((u32) areaWidth);
    u32 lh = areaHeight >= 8 ? 3 : areaHeight < 1 ? 0 : 31 - (u32) __builtin_clz((u32) areaHeight);
    // The last argument packs the OBJ width and height shifts of the first OBJ into a word (bytes 0 and 1).
    u32 stride = (u32) objs[lh][lw][0] | ((u32) objs[lh][lw][1] << 8);
    InitCharCanvas(pCC, charBase, areaWidth, areaHeight, (u32) colorMode, DrawGlyph1D, ClearContinuous, ClearArea1D,
                   stride);
}

// ------------------------------------------------------------------------------------------------ SHA-1 blocks
static inline u32 rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

void DGTi_hash2_arm4_small(DGTHash2Context *ctx, u8 *data, int size) {
    for (; size >= 64; size -= 64, data += 64) {
        u32 w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (u32) data[i * 4] << 24 | (u32) data[i * 4 + 1] << 16 | (u32) data[i * 4 + 2] << 8 | data[i * 4 + 3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        u32 a = ctx->mWords[0], b = ctx->mWords[1], c = ctx->mWords[2], d = ctx->mWords[3], e = ctx->mWords[4];
        for (int i = 0; i < 80; i++) {
            u32 f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            u32 t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        ctx->mWords[0] += a;
        ctx->mWords[1] += b;
        ctx->mWords[2] += c;
        ctx->mWords[3] += d;
        ctx->mWords[4] += e;
    }
}

// The point link a CPU kart's AI is reset to, passed on in r1 on the DS (see tools/portify.py).
void *g_port_ai_reset_link;

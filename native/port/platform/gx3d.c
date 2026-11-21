// The DS geometry engine: everything the 3D hardware does before rasterisation.
//
// Commands arrive through the command ports (0x04000440..0x040005ff), the packed GXFIFO port (0x04000400) and DMA,
// and run at once. Vertices are transformed to clip space and lit; polygons are assembled, culled and stored with the
// state the rasteriser needs. Clipping is left to the rasteriser (OpenGL clips against the same planes), which keeps
// whole polygons for the frame interpolation and the widescreen projection.
#include "port.h"
#include "gx.h"

#include <string.h>

typedef int32_t fx32;
typedef struct Mtx {
    fx32 m[16]; // row-major, row vectors (v' = v * M), 20.12
} Mtx;

static const uint8_t s_param_count[256] = {
    [0x10] = 1, [0x11] = 0, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0, [0x16] = 16, [0x17] = 12, [0x18] = 16,
    [0x19] = 12, [0x1a] = 9, [0x1b] = 3, [0x1c] = 3, [0x20] = 1, [0x21] = 1, [0x22] = 1, [0x23] = 2, [0x24] = 1,
    [0x25] = 1, [0x26] = 1, [0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2a] = 1, [0x2b] = 1, [0x30] = 1, [0x31] = 1,
    [0x32] = 1, [0x33] = 1, [0x34] = 32, [0x40] = 1, [0x41] = 0, [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2,
    [0x72] = 1,
};
static bool valid_command(uint32_t c) { return c == 0x11 || c == 0x15 || c == 0x41 || s_param_count[c] != 0; }

static struct {
    // matrices
    int mode;
    Mtx proj, pos, vec, tex, clip;
    Mtx proj_stack[1], pos_stack[32], vec_stack[32], tex_stack[1];
    int proj_sp, pos_sp, tex_sp;
    bool stack_error;
    bool clip_dirty;
    // current vertex state
    uint8_t color[3]; // 0..31
    int16_t s, t;     // texcoord as given
    int16_t ts, tt;   // after the texture matrix (modes 1 and 2)
    int16_t vx, vy, vz;
    uint32_t polyattr_pending, polyattr, teximage, pltt;
    // lighting
    uint16_t diffuse, ambient, specular, emission;
    bool use_shininess_table, set_vertex_color;
    int32_t light_dir[4][3];   // transformed, s.9
    int32_t light_half[4][3];  // half vectors
    uint16_t light_color[4];
    uint8_t shininess[128];
    // primitive assembly
    int prim;       // 0 tri, 1 quad, 2 tri strip, 3 quad strip, -1 none
    GxVertex strip[4];
    int strip_count;
    uint32_t strip_total;
    uint8_t viewport[4];
    // command decoding
    uint32_t cmd, params[32];
    int param_index;
    uint32_t packed, packed_left;
    // output
    Gx3dFrame frames[2];
    int building; // index of the list being built
    int displayed;
    bool swap_pending;
    uint32_t swap_param;
    uint32_t draw_tag;
    uint32_t prim_index;   // polygons begun in the current vertex block
    int32_t proj_aspect;   // of the projection matrix
    bool proj_aspect_dirty;
} G;

// ------------------------------------------------------------------------------------------------ matrix helpers
static void mtx_identity(Mtx *m) {
    memset(m, 0, sizeof(*m));
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 0x1000;
}

// r = a * b
static void mtx_mul(Mtx *r, const Mtx *a, const Mtx *b) {
    Mtx t;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int64_t s = 0;
            for (int k = 0; k < 4; k++) {
                s += (int64_t) a->m[i * 4 + k] * b->m[k * 4 + j];
            }
            t.m[i * 4 + j] = (fx32) (s >> 12);
        }
    }
    *r = t;
}

static void load_params(Mtx *m, const uint32_t *p, int kind) {
    mtx_identity(m);
    switch (kind) {
    case 44:
        for (int i = 0; i < 16; i++) m->m[i] = (fx32) p[i];
        break;
    case 43:
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 3; c++) m->m[r * 4 + c] = (fx32) p[r * 3 + c];
        break;
    case 33:
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) m->m[r * 4 + c] = (fx32) p[r * 3 + c];
        break;
    }
}

static void update_result_registers(void);

static void apply_load(const Mtx *n) {
    switch (G.mode) {
    case 0: G.proj = *n; G.proj_aspect_dirty = true; break;
    case 1: G.pos = *n; break;
    case 2: G.pos = *n; G.vec = *n; break;
    case 3: G.tex = *n; break;
    }
    G.clip_dirty = true;
}

// Current = N * Current. Mode 2 multiplies the direction matrix too (except for SCALE).
static void apply_mult(const Mtx *n, bool scale) {
    switch (G.mode) {
    case 0: mtx_mul(&G.proj, n, &G.proj); G.proj_aspect_dirty = true; break;
    case 1: mtx_mul(&G.pos, n, &G.pos); break;
    case 2:
        mtx_mul(&G.pos, n, &G.pos);
        if (!scale) {
            mtx_mul(&G.vec, n, &G.vec);
        }
        break;
    case 3: mtx_mul(&G.tex, n, &G.tex); break;
    }
    G.clip_dirty = true;
}

static void update_clip(void) {
    if (G.clip_dirty) {
        mtx_mul(&G.clip, &G.pos, &G.proj);
        G.clip_dirty = false;
    }
}

// ------------------------------------------------------------------------------------------------ lighting
static void rgb15(uint16_t c, int out[3]) {
    out[0] = c & 31;
    out[1] = (c >> 5) & 31;
    out[2] = (c >> 10) & 31;
}

static int32_t sext10(uint32_t v) { return (int32_t) (v << 22) >> 22; }

static void light_vertex(uint32_t normal) {
    int32_t n0[3] = {sext10(normal), sext10(normal >> 10), sext10(normal >> 20)}; // s.9
    int32_t n[3];
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++) {
            s += (int64_t) n0[k] * G.vec.m[k * 4 + i];
        }
        n[i] = (int32_t) (s >> 12); // still s.9
    }
    int dif[3], amb[3], spe[3], emi[3], col[3] = {0, 0, 0};
    rgb15(G.diffuse, dif);
    rgb15(G.ambient, amb);
    rgb15(G.specular, spe);
    rgb15(G.emission, emi);
    int out[3] = {emi[0] << 9, emi[1] << 9, emi[2] << 9};
    for (int l = 0; l < 4; l++) {
        if (!(G.polyattr & (1u << l))) {
            continue;
        }
        int lc[3];
        rgb15(G.light_color[l], lc);
        int32_t d = -(G.light_dir[l][0] * n[0] + G.light_dir[l][1] * n[1] + G.light_dir[l][2] * n[2]) >> 9;
        if (d < 0) d = 0;
        if (d > 0x1ff) d = 0x1ff;
        int32_t sh = -(G.light_half[l][0] * n[0] + G.light_half[l][1] * n[1] + G.light_half[l][2] * n[2]) >> 9;
        if (sh < 0) sh = 0;
        sh = (sh * sh) >> 9; // squared
        if (sh > 0x1ff) sh = 0x1ff;
        if (G.use_shininess_table) {
            sh = G.shininess[sh >> 2] << 1;
        }
        for (int i = 0; i < 3; i++) {
            out[i] += spe[i] * lc[i] * sh / 31 + dif[i] * lc[i] * d / 31 + ((amb[i] * lc[i]) << 9) / 31;
        }
    }
    for (int i = 0; i < 3; i++) {
        col[i] = out[i] >> 9;
        if (col[i] > 31) col[i] = 31;
        G.color[i] = (uint8_t) col[i];
    }
}

// ------------------------------------------------------------------------------------------------ polygons
static Gx3dFrame *building(void) { return &G.frames[G.building]; }

static int32_t det3(int64_t a0, int64_t a1, int64_t a2, int64_t b0, int64_t b1, int64_t b2, int64_t c0, int64_t c1,
                    int64_t c2) {
    // Sign of the determinant, computed in floating point to avoid overflow of 20.12 products.
    double d = (double) a0 * ((double) b1 * c2 - (double) b2 * c1) - (double) a1 * ((double) b0 * c2 - (double) b2 * c0) +
               (double) a2 * ((double) b0 * c1 - (double) b1 * c0);
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

// Set by the game around what it draws (see tools/portify.py): the identity of the object, so that the renderer
// pairs each object's pieces with the same object's in the previous frame (frame interpolation).
unsigned int g_port_draw_id;

static void emit_polygon(const GxVertex *v, int count) {
    Gx3dFrame *f = building();
    uint32_t index_in_block = G.prim_index++;
    bool hidden = false;
    // Entirely outside one clip plane: dropped, as the DS does (kept hidden).
    for (int plane = 0; plane < 6 && !hidden; plane++) {
        int outside = 0;
        for (int i = 0; i < count; i++) {
            int32_t c = plane < 2 ? v[i].x : plane < 4 ? v[i].y : v[i].z;
            if (plane & 1 ? c > v[i].w : c < -v[i].w) {
                outside++;
            }
        }
        if (outside == count) {
            hidden = true;
        }
    }
    // Facing: winding in homogeneous coordinates (y up), counter-clockwise on screen is front.
    int sign = det3(v[0].x, v[0].y, v[0].w, v[1].x, v[1].y, v[1].w, v[2].x, v[2].y, v[2].w);
    if (sign == 0 && count == 4) {
        sign = det3(v[0].x, v[0].y, v[0].w, v[2].x, v[2].y, v[2].w, v[3].x, v[3].y, v[3].w);
    }
    bool front = sign >= 0;
    uint32_t attr = G.polyattr;
    if ((front && !(attr & 0x80)) || (!front && !(attr & 0x40))) {
        hidden = true; // culled (bit 7 renders front faces, bit 6 back faces)
    }
    if (f->polygon_count >= GX_MAX_POLYGONS || f->vertex_count + count > GX_MAX_VERTICES) {
        return;
    }
    GxPolygon *p = &f->polygons[f->polygon_count++];
    p->first = f->vertex_count;
    p->count = (uint8_t) count;
    memcpy(p->viewport, G.viewport, 4);
    p->front = front;
    p->hidden = hidden;
    p->attr = attr;
    p->teximage = G.teximage;
    p->pltt = G.pltt;
    p->tag = index_in_block;
    p->obj = g_port_draw_id;
    // The width of the view the projection maps to the screen, relative to its height: wider than 4:3 when the game
    // projects the widescreen view (g_port_aspect).
    if (G.proj_aspect_dirty) {
        G.proj_aspect_dirty = false;
        int64_t sx = G.proj.m[0] < 0 ? -(int64_t) G.proj.m[0] : G.proj.m[0];
        int64_t sy = G.proj.m[5] < 0 ? -(int64_t) G.proj.m[5] : G.proj.m[5];
        G.proj_aspect = sx ? (int32_t) (sy * 4096 / sx) : 0x1555;
        if (G.proj_aspect < 0x800 || G.proj_aspect > 0x4000) G.proj_aspect = 0x1555; // not a screen projection
    }
    p->aspect = G.proj_aspect;
    if (!f->proj_aspect) f->proj_aspect = G.proj_aspect;
    memcpy(&f->vertices[f->vertex_count], v, sizeof(GxVertex) * count);
    f->vertex_count += count;
}

static void submit_vertex(void) {
    update_clip();
    GxVertex v;
    const fx32 *m = G.clip.m;
    int64_t x = G.vx, y = G.vy, z = G.vz;
    v.x = (int32_t) ((x * m[0] + y * m[4] + z * m[8] + 0x1000ll * m[12]) >> 12);
    v.y = (int32_t) ((x * m[1] + y * m[5] + z * m[9] + 0x1000ll * m[13]) >> 12);
    v.z = (int32_t) ((x * m[2] + y * m[6] + z * m[10] + 0x1000ll * m[14]) >> 12);
    v.w = (int32_t) ((x * m[3] + y * m[7] + z * m[11] + 0x1000ll * m[15]) >> 12);
    v.r = G.color[0];
    v.g = G.color[1];
    v.b = G.color[2];
    v.pad = 0;
    // Texture coordinates.
    uint32_t mode = G.teximage >> 30;
    if (mode == 3) {
        const fx32 *t = G.tex.m;
        v.s = (int16_t) (((x * t[0] + y * t[4] + z * t[8]) >> 24) + G.s);
        v.t = (int16_t) (((x * t[1] + y * t[5] + z * t[9]) >> 24) + G.t);
    } else if (mode == 0) {
        v.s = G.s;
        v.t = G.t;
    } else {
        v.s = G.ts;
        v.t = G.tt;
    }
    if (G.prim < 0) {
        return;
    }
    G.strip[G.strip_count++] = v;
    switch (G.prim) {
    case 0: // separate triangles
        if (G.strip_count == 3) {
            emit_polygon(G.strip, 3);
            G.strip_count = 0;
        }
        break;
    case 1: // separate quads
        if (G.strip_count == 4) {
            emit_polygon(G.strip, 4);
            G.strip_count = 0;
        }
        break;
    case 2: // triangle strip: every second triangle is wound the other way
        if (G.strip_count == 3) {
            if (G.strip_total & 1) {
                GxVertex t[3] = {G.strip[1], G.strip[0], G.strip[2]};
                emit_polygon(t, 3);
            } else {
                emit_polygon(G.strip, 3);
            }
            G.strip_total++;
            G.strip[0] = G.strip[1];
            G.strip[1] = G.strip[2];
            G.strip_count = 2;
        }
        break;
    case 3: // quad strip: vertices 0 1 3 2 form the quad
        if (G.strip_count == 4) {
            GxVertex q[4] = {G.strip[0], G.strip[1], G.strip[3], G.strip[2]};
            emit_polygon(q, 4);
            G.strip[0] = G.strip[2];
            G.strip[1] = G.strip[3];
            G.strip_count = 2;
        }
        break;
    }
}

// ------------------------------------------------------------------------------------------------ tests and results
static void update_result_registers(void) {
    update_clip();
    for (int i = 0; i < 16; i++) {
        IO32(0x640 + i * 4) = (uint32_t) G.clip.m[i];
    }
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            IO32(0x680 + (r * 3 + c) * 4) = (uint32_t) G.vec.m[r * 4 + c];
        }
    }
}

static void update_gxstat(void) {
    uint32_t stat = IO32(0x600) & 0x3; // test bits
    stat |= (uint32_t) (G.pos_sp & 0x1f) << 8;
    stat |= (uint32_t) (G.proj_sp & 1) << 13;
    stat |= G.stack_error ? 0x8000 : 0;
    stat |= (1u << 25) | (1u << 26); // FIFO less than half full, empty
    stat |= IO32(0x600) & 0xc0000000; // FIFO interrupt mode
    IO32(0x600) = stat;
    IO32(0x604) = building()->polygon_count | (building()->vertex_count << 16);
}

static void box_test(void) {
    update_clip();
    int16_t p[6] = {(int16_t) G.params[0], (int16_t) (G.params[0] >> 16), (int16_t) G.params[1],
                    (int16_t) (G.params[1] >> 16), (int16_t) G.params[2], (int16_t) (G.params[2] >> 16)};
    int64_t x0 = p[0], y0 = p[1], z0 = p[2], x1 = p[0] + p[3], y1 = p[1] + p[4], z1 = p[2] + p[5];
    const fx32 *m = G.clip.m;
    int outside_mask = 0x3f; // a plane that has every corner outside
    for (int i = 0; i < 8; i++) {
        int64_t x = i & 1 ? x1 : x0, y = i & 2 ? y1 : y0, z = i & 4 ? z1 : z0;
        int64_t cx = (x * m[0] + y * m[4] + z * m[8] + 0x1000ll * m[12]) >> 12;
        int64_t cy = (x * m[1] + y * m[5] + z * m[9] + 0x1000ll * m[13]) >> 12;
        int64_t cz = (x * m[2] + y * m[6] + z * m[10] + 0x1000ll * m[14]) >> 12;
        int64_t cw = (x * m[3] + y * m[7] + z * m[11] + 0x1000ll * m[15]) >> 12;
        int out = (cx < -cw) | (cx > cw) << 1 | (cy < -cw) << 2 | (cy > cw) << 3 | (cz < -cw) << 4 | (cz > cw) << 5;
        outside_mask &= out;
    }
    if (outside_mask) {
        IO32(0x600) &= ~2u;
    } else {
        IO32(0x600) |= 2u;
    }
}

static void pos_test(void) {
    G.vx = (int16_t) G.params[0];
    G.vy = (int16_t) (G.params[0] >> 16);
    G.vz = (int16_t) G.params[1];
    update_clip();
    const fx32 *m = G.clip.m;
    int64_t x = G.vx, y = G.vy, z = G.vz;
    for (int i = 0; i < 4; i++) {
        IO32(0x620 + i * 4) = (uint32_t) ((x * m[i] + y * m[4 + i] + z * m[8 + i] + 0x1000ll * m[12 + i]) >> 12);
    }
}

static void vec_test(void) {
    uint32_t p = G.params[0];
    int32_t n[3] = {sext10(p), sext10(p >> 10), sext10(p >> 20)};
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++) {
            s += (int64_t) (n[k] << 3) * G.vec.m[k * 4 + i];
        }
        int32_t r = (int32_t) (s >> 12);
        if (r & 0x1000) r |= ~0xfff; // 4.12 with the sign repeated above
        IO16(0x630 + i * 2) = (uint16_t) r;
    }
}

// ------------------------------------------------------------------------------------------------ commands
int g_gx_log; // tests: log this many geometry commands
static void execute(uint32_t cmd) {
    if (g_gx_log > 0) {
        g_gx_log--;
        port_log("gx %02x %08x %08x %08x mode %d", cmd, G.params[0], G.params[1], G.params[2], G.mode);
        if (cmd == 0x16) {
            for (int i = 0; i < 16; i += 4) port_log("   %d %d %d %d", G.params[i], G.params[i + 1], G.params[i + 2], G.params[i + 3]);
        }
    }
    const uint32_t *p = G.params;
    Mtx n;
    switch (cmd) {
    case 0x10: G.mode = p[0] & 3; break;
    case 0x11: // PUSH
        if (G.mode == 0) {
            G.proj_stack[0] = G.proj;
            G.proj_sp = 1;
        } else if (G.mode == 3) {
            G.tex_stack[0] = G.tex;
            G.tex_sp = 1;
        } else {
            if (G.pos_sp >= 31) G.stack_error = true;
            G.pos_stack[G.pos_sp & 31] = G.pos;
            G.vec_stack[G.pos_sp & 31] = G.vec;
            G.pos_sp = (G.pos_sp + 1) & 63;
        }
        break;
    case 0x12: { // POP
        if (G.mode == 0) {
            G.proj = G.proj_stack[0];
            G.proj_aspect_dirty = true;
            G.proj_sp = 0;
        } else if (G.mode == 3) {
            G.tex = G.tex_stack[0];
            G.tex_sp = 0;
        } else {
            int off = (int) (p[0] << 26) >> 26;
            G.pos_sp = (G.pos_sp - off) & 63;
            if (G.pos_sp >= 31) G.stack_error = true;
            G.pos = G.pos_stack[G.pos_sp & 31];
            G.vec = G.vec_stack[G.pos_sp & 31];
        }
        G.clip_dirty = true;
        break;
    }
    case 0x13: // STORE
        if (G.mode == 0) {
            G.proj_stack[0] = G.proj;
        } else if (G.mode == 3) {
            G.tex_stack[0] = G.tex;
        } else {
            uint32_t i = p[0] & 31;
            if (i == 31) G.stack_error = true;
            G.pos_stack[i] = G.pos;
            G.vec_stack[i] = G.vec;
        }
        break;
    case 0x14: // RESTORE
        if (G.mode == 0) {
            G.proj = G.proj_stack[0];
            G.proj_aspect_dirty = true;
        } else if (G.mode == 3) {
            G.tex = G.tex_stack[0];
        } else {
            uint32_t i = p[0] & 31;
            if (i == 31) G.stack_error = true;
            G.pos = G.pos_stack[i];
            G.vec = G.vec_stack[i];
        }
        G.clip_dirty = true;
        break;
    case 0x15: mtx_identity(&n); apply_load(&n); break;
    case 0x16: load_params(&n, p, 44); apply_load(&n); break;
    case 0x17: load_params(&n, p, 43); apply_load(&n); break;
    case 0x18: load_params(&n, p, 44); apply_mult(&n, false); break;
    case 0x19: load_params(&n, p, 43); apply_mult(&n, false); break;
    case 0x1a: load_params(&n, p, 33); apply_mult(&n, false); break;
    case 0x1b: // SCALE
        mtx_identity(&n);
        n.m[0] = (fx32) p[0];
        n.m[5] = (fx32) p[1];
        n.m[10] = (fx32) p[2];
        apply_mult(&n, true);
        break;
    case 0x1c: // TRANS
        mtx_identity(&n);
        n.m[12] = (fx32) p[0];
        n.m[13] = (fx32) p[1];
        n.m[14] = (fx32) p[2];
        apply_mult(&n, false);
        break;
    case 0x20: { // COLOR
        int c[3];
        rgb15((uint16_t) p[0], c);
        G.color[0] = (uint8_t) c[0];
        G.color[1] = (uint8_t) c[1];
        G.color[2] = (uint8_t) c[2];
        break;
    }
    case 0x21: // NORMAL
        if ((G.teximage >> 30) == 2) {
            int32_t n0[3] = {sext10(p[0]), sext10(p[0] >> 10), sext10(p[0] >> 20)};
            const fx32 *t = G.tex.m;
            G.ts = (int16_t) ((((int64_t) n0[0] * t[0] + (int64_t) n0[1] * t[4] + (int64_t) n0[2] * t[8]) >> 21) + G.s);
            G.tt = (int16_t) ((((int64_t) n0[0] * t[1] + (int64_t) n0[1] * t[5] + (int64_t) n0[2] * t[9]) >> 21) + G.t);
        }
        light_vertex(p[0]);
        break;
    case 0x22: // TEXCOORD
        G.s = (int16_t) p[0];
        G.t = (int16_t) (p[0] >> 16);
        if ((G.teximage >> 30) == 1) {
            const fx32 *t = G.tex.m;
            int64_t s = G.s, tt = G.t;
            // (S, T, 1/16, 1/16) times the matrix: with S and T in 1/16 texels, the translation rows count once.
            G.ts = (int16_t) ((s * t[0] + tt * t[4] + t[8] + t[12]) >> 12);
            G.tt = (int16_t) ((s * t[1] + tt * t[5] + t[9] + t[13]) >> 12);
        } else {
            G.ts = G.s;
            G.tt = G.t;
        }
        break;
    case 0x23:
        G.vx = (int16_t) p[0];
        G.vy = (int16_t) (p[0] >> 16);
        G.vz = (int16_t) p[1];
        submit_vertex();
        break;
    case 0x24: // VTX_10: 4.6 fixed
        G.vx = (int16_t) ((p[0] & 0x3ff) << 6);
        G.vy = (int16_t) (((p[0] >> 10) & 0x3ff) << 6);
        G.vz = (int16_t) (((p[0] >> 20) & 0x3ff) << 6);
        submit_vertex();
        break;
    case 0x25: G.vx = (int16_t) p[0]; G.vy = (int16_t) (p[0] >> 16); submit_vertex(); break;
    case 0x26: G.vx = (int16_t) p[0]; G.vz = (int16_t) (p[0] >> 16); submit_vertex(); break;
    case 0x27: G.vy = (int16_t) p[0]; G.vz = (int16_t) (p[0] >> 16); submit_vertex(); break;
    case 0x28: // VTX_DIFF: 10-bit signed offsets in the vertex units (4.12)
        G.vx = (int16_t) (G.vx + sext10(p[0]));
        G.vy = (int16_t) (G.vy + sext10(p[0] >> 10));
        G.vz = (int16_t) (G.vz + sext10(p[0] >> 20));
        submit_vertex();
        break;
    case 0x29: G.polyattr_pending = p[0]; break;
    case 0x2a: G.teximage = p[0]; break;
    case 0x2b: G.pltt = p[0] & 0x1fff; break;
    case 0x30: // DIF_AMB
        G.diffuse = (uint16_t) (p[0] & 0x7fff);
        G.ambient = (uint16_t) ((p[0] >> 16) & 0x7fff);
        if (p[0] & 0x8000) {
            int c[3];
            rgb15(G.diffuse, c);
            G.color[0] = (uint8_t) c[0];
            G.color[1] = (uint8_t) c[1];
            G.color[2] = (uint8_t) c[2];
        }
        break;
    case 0x31: // SPE_EMI
        G.specular = (uint16_t) (p[0] & 0x7fff);
        G.emission = (uint16_t) ((p[0] >> 16) & 0x7fff);
        G.use_shininess_table = (p[0] >> 15) & 1;
        break;
    case 0x32: { // LIGHT_VECTOR, transformed by the direction matrix
        int l = p[0] >> 30;
        int32_t d0[3] = {sext10(p[0]), sext10(p[0] >> 10), sext10(p[0] >> 20)};
        for (int i = 0; i < 3; i++) {
            int64_t s = 0;
            for (int k = 0; k < 3; k++) {
                s += (int64_t) d0[k] * G.vec.m[k * 4 + i];
            }
            G.light_dir[l][i] = (int32_t) (s >> 12);
        }
        // Half vector between the light and the line of sight (0, 0, -1).
        G.light_half[l][0] = G.light_dir[l][0] / 2;
        G.light_half[l][1] = G.light_dir[l][1] / 2;
        G.light_half[l][2] = (G.light_dir[l][2] - 0x200) / 2;
        break;
    }
    case 0x33: G.light_color[p[0] >> 30] = (uint16_t) (p[0] & 0x7fff); break;
    case 0x34:
        for (int i = 0; i < 32; i++) {
            G.shininess[i * 4 + 0] = (uint8_t) p[i];
            G.shininess[i * 4 + 1] = (uint8_t) (p[i] >> 8);
            G.shininess[i * 4 + 2] = (uint8_t) (p[i] >> 16);
            G.shininess[i * 4 + 3] = (uint8_t) (p[i] >> 24);
        }
        break;
    case 0x40: // BEGIN_VTXS
        G.prim = (int) (p[0] & 3);
        G.strip_count = 0;
        G.strip_total = 0;
        G.polyattr = G.polyattr_pending;
        G.prim_index = 0;
        G.draw_tag = G.draw_tag * 31 + (uint32_t) G.clip.m[12] * 7 + (uint32_t) G.clip.m[13];
        break;
    case 0x41: break; // END_VTXS: nothing to do
    case 0x50: // SWAP_BUFFERS
        G.swap_pending = true;
        G.swap_param = p[0] & 3;
        break;
    case 0x60: // VIEWPORT
        G.viewport[0] = (uint8_t) p[0];
        G.viewport[1] = (uint8_t) (p[0] >> 8);
        G.viewport[2] = (uint8_t) (p[0] >> 16);
        G.viewport[3] = (uint8_t) (p[0] >> 24);
        break;
    case 0x70: box_test(); break;
    case 0x71: pos_test(); break;
    case 0x72: vec_test(); break;
    default: break;
    }
    if (cmd >= 0x10 && cmd <= 0x1c) {
        update_result_registers();
    }
    update_gxstat();
}

// One parameter word for the command `cmd`: runs the command when its parameters are complete.
static void feed(uint32_t cmd, uint32_t param) {
    if (G.param_index == 0) {
        G.cmd = cmd;
    }
    G.params[G.param_index++] = param;
    if (G.param_index >= (s_param_count[cmd] ? s_param_count[cmd] : 1)) {
        G.param_index = 0;
        execute(cmd);
    }
}

void gx3d_port_write(uint32_t addr, uint32_t value) {
    uint32_t cmd = (addr - 0x04000400) >> 2;
    if (!valid_command(cmd)) {
        return;
    }
    if (G.param_index && G.cmd != cmd) {
        G.param_index = 0; // a different command interrupts an incomplete one
    }
    feed(cmd, value);
}

// GXFIFO: a word holding up to four command ids, then their parameters. A command without parameters takes no word,
// except when the whole packed word is 0.
void gx3d_fifo_write(uint32_t word) {
    if (G.packed_left == 0) {
        G.packed = word;
        G.packed_left = 4;
        G.param_index = 0;
        // Skip leading commands with no parameters.
        while (G.packed_left) {
            uint32_t cmd = G.packed & 0xff;
            if (s_param_count[cmd] != 0) {
                return;
            }
            if (valid_command(cmd)) {
                execute(cmd);
            }
            G.packed >>= 8;
            G.packed_left--;
            if (G.packed == 0) {
                G.packed_left = 0; // the remaining ids are NOPs
            }
        }
        return;
    }
    uint32_t cmd = G.packed & 0xff;
    G.params[G.param_index++] = word;
    if (G.param_index < s_param_count[cmd]) {
        return;
    }
    G.param_index = 0;
    execute(cmd);
    G.packed >>= 8;
    G.packed_left--;
    while (G.packed_left) {
        cmd = G.packed & 0xff;
        if (G.packed == 0) {
            G.packed_left = 0;
            break;
        }
        if (s_param_count[cmd] != 0) {
            return;
        }
        if (valid_command(cmd)) {
            execute(cmd);
        }
        G.packed >>= 8;
        G.packed_left--;
    }
}

void gx3d_command(uint32_t cmd, uint32_t param) { feed(cmd, param); }

// ------------------------------------------------------------------------------------------------ frames
void gx3d_init(void) {
    memset(&G, 0, sizeof(G));
    mtx_identity(&G.proj);
    G.proj_aspect_dirty = true;
    mtx_identity(&G.pos);
    mtx_identity(&G.vec);
    mtx_identity(&G.tex);
    mtx_identity(&G.clip);
    G.prim = -1;
    G.viewport[2] = 255;
    G.viewport[3] = 191;
    G.building = 0;
    G.displayed = 1;
    update_gxstat();
}

const Gx3dFrame *gx3d_displayed(void) { return &G.frames[G.displayed]; }

static void latch_render_registers(Gx3dFrame *f) {
    f->disp3dcnt = IO16(0x060);
    f->clear_color = IO32(0x350);
    f->clear_depth = IO16(0x354);
    f->clrimage_offset = IO16(0x356);
    f->fog_color = IO32(0x358);
    f->fog_offset = IO16(0x35c);
    memcpy(f->fog_table, (const void *) (uintptr_t) 0x04000360, 32);
    memcpy(f->edge_color, (const void *) (uintptr_t) 0x04000330, 16);
    memcpy(f->toon_table, (const void *) (uintptr_t) 0x04000380, 64);
    f->alpha_ref = IO8(0x340) & 31;
}

// A frame the game finished without SWAP_BUFFERS (a multiplayer frame whose shared data was late): the DS keeps
// showing the previous picture. Its geometry is dropped, so that it does not pile up under the next frame's (the same
// scene twice: overflowing lists, doubled translucent polygons, model pieces the interpolation cannot pair).
void gx3d_discard(void) {
    if (G.swap_pending) {
        return;
    }
    Gx3dFrame *f = building();
    f->polygon_count = 0;
    f->proj_aspect = 0;
    f->vertex_count = 0;
    G.draw_tag = 0;
    update_gxstat();
}

// VBlank: a list ended by SWAP_BUFFERS becomes the displayed one and a new list starts.
void gx3d_vblank(void) {
    if (!G.swap_pending) {
        return;
    }
    G.swap_pending = false;
    Gx3dFrame *done = building();
    done->swap_param = G.swap_param;
    latch_render_registers(done);
    static uint64_t serial;
    done->serial = ++serial;
    G.displayed = G.building;
    G.building ^= 1;
    Gx3dFrame *next = building();
    next->polygon_count = 0;
    next->proj_aspect = 0;
    next->vertex_count = 0;
    G.draw_tag = 0;
    update_gxstat();
}

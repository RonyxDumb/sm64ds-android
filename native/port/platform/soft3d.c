// A plain software rasteriser for the 3D polygon lists at 256x192: used by the host test build to look at frames,
// not for the phone (port/platform/android renders with OpenGL ES). Near-plane clipping, depth buffer, perspective-
// correct texturing and colours, modulate/decal, opaque then translucent polygons.
#include "port.h"
#include "gpu2d.h"
#include "tex.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct V {
    float x, y, z, w, s, t, r, g, b;
} V;

typedef struct TexEntry {
    uint32_t teximage, pltt;
    uint32_t *texels;
} TexEntry;

static TexEntry s_cache[512];
static int s_cache_count;

static const uint32_t *texture(const VramMap *map, const uint8_t *vram, uint32_t teximage, uint32_t pltt) {
    uint32_t key = teximage & 0x3fffffff;
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].teximage == key && s_cache[i].pltt == pltt) {
            return s_cache[i].texels;
        }
    }
    if (s_cache_count == 512) {
        return NULL;
    }
    uint32_t w = TEX_WIDTH(teximage), h = TEX_HEIGHT(teximage);
    uint32_t *t = malloc(w * h * 4);
    if (!tex_decode(map, vram, teximage, pltt, t)) {
        free(t);
        return NULL;
    }
    s_cache[s_cache_count].teximage = key;
    s_cache[s_cache_count].pltt = pltt;
    s_cache[s_cache_count].texels = t;
    s_cache_count++;
    return t;
}

static void cache_clear(void) {
    for (int i = 0; i < s_cache_count; i++) free(s_cache[i].texels);
    s_cache_count = 0;
}

static float s_depth[256 * 192];

static uint32_t sample(const uint32_t *tex, uint32_t teximage, float s, float t) {
    int w = (int) TEX_WIDTH(teximage), h = (int) TEX_HEIGHT(teximage);
    int u = (int) floorf(s / 16.0f), v = (int) floorf(t / 16.0f);
    if (teximage & (1u << 16)) {
        if (teximage & (1u << 18)) {
            int p = ((u % (2 * w)) + 2 * w) % (2 * w);
            u = p >= w ? 2 * w - 1 - p : p;
        } else {
            u = ((u % w) + w) % w;
        }
    } else {
        u = u < 0 ? 0 : u >= w ? w - 1 : u;
    }
    if (teximage & (1u << 17)) {
        if (teximage & (1u << 19)) {
            int p = ((v % (2 * h)) + 2 * h) % (2 * h);
            v = p >= h ? 2 * h - 1 - p : p;
        } else {
            v = ((v % h) + h) % h;
        }
    } else {
        v = v < 0 ? 0 : v >= h ? h - 1 : v;
    }
    return tex[v * w + u];
}

static void raster_triangle(const V *a, const V *b, const V *c, const GxPolygon *p, const uint32_t *tex,
                            uint32_t *out, bool translucent_pass) {
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-6f) return;
    int minx = (int) floorf(fminf(a->x, fminf(b->x, c->x))), maxx = (int) ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int miny = (int) floorf(fminf(a->y, fminf(b->y, c->y))), maxy = (int) ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > 255) maxx = 255;
    if (maxy > 191) maxy = 191;
    uint32_t alpha = (p->attr >> 16) & 31;
    if (alpha == 0) alpha = 31; // wireframe drawn solid here
    uint32_t mode = (p->attr >> 4) & 3;
    bool depth_equal = p->attr & (1u << 14);
    bool update_depth = !translucent_pass || (p->attr & (1u << 11));
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b->x - px) * (c->y - py) - (b->y - py) * (c->x - px)) / area;
            float w1 = ((c->x - px) * (a->y - py) - (c->y - py) * (a->x - px)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a->z + w1 * b->z + w2 * c->z;
            float *dz = &s_depth[y * 256 + x];
            if (depth_equal ? fabsf(z - *dz) > 1e-4f : z >= *dz) continue;
            // Perspective-correct attributes (x/y/z are already divided; w holds 1/w).
            float iw = w0 * a->w + w1 * b->w + w2 * c->w;
            float k0 = w0 * a->w / iw, k1 = w1 * b->w / iw, k2 = w2 * c->w / iw;
            float r = k0 * a->r + k1 * b->r + k2 * c->r;
            float g = k0 * a->g + k1 * b->g + k2 * c->g;
            float bl = k0 * a->b + k1 * b->b + k2 * c->b;
            uint32_t cr = (uint32_t) (r * 255 / 31), cg = (uint32_t) (g * 255 / 31), cb = (uint32_t) (bl * 255 / 31);
            uint32_t ca = alpha;
            if (tex) {
                float s = k0 * a->s + k1 * b->s + k2 * c->s, t = k0 * a->t + k1 * b->t + k2 * c->t;
                uint32_t texel = sample(tex, p->teximage, s, t);
                uint32_t ta = texel >> 24;
                if (ta == 0) continue;
                uint32_t tr = texel & 255, tg = (texel >> 8) & 255, tb = (texel >> 16) & 255;
                if (mode == 1) { // decal
                    cr = (tr * ta + cr * (31 - ta)) / 31;
                    cg = (tg * ta + cg * (31 - ta)) / 31;
                    cb = (tb * ta + cb * (31 - ta)) / 31;
                } else {
                    cr = cr * tr / 255;
                    cg = cg * tg / 255;
                    cb = cb * tb / 255;
                    ca = ca * ta / 31;
                }
            }
            if (mode == 3) continue; // shadow polygons need the stencil logic: skipped here
            uint32_t *o = &out[y * 256 + x];
            if (ca < 31 && (*o >> 24)) {
                uint32_t d = *o;
                cr = (cr * ca + (d & 255) * (31 - ca)) / 31;
                cg = (cg * ca + ((d >> 8) & 255) * (31 - ca)) / 31;
                cb = (cb * ca + ((d >> 16) & 255) * (31 - ca)) / 31;
                uint32_t da = d >> 24;
                ca = ca > da ? ca : da;
            }
            if (update_depth) *dz = z;
            *o = cr | cg << 8 | cb << 16 | ca << 24;
        }
    }
}

// Clips a polygon to z >= -w (the near plane), in clip space.
static int clip_near(const V *in, int n, V *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const V *a = &in[i], *b = &in[(i + 1) % n];
        float da = a->z + a->w, db = b->z + b->w;
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            V v;
            float *pv = &v.x;
            const float *pa = &a->x, *pb = &b->x;
            for (int k = 0; k < 9; k++) pv[k] = pa[k] + (pb[k] - pa[k]) * t;
            out[m++] = v;
        }
    }
    return m;
}

void soft3d_render(const Gx3dFrame *frame, const FrameSnapshot *f, uint32_t *out) {
    VramMap map;
    vram_map_build(&map, f->vramcnt);
    cache_clear();
    uint32_t clear = frame->clear_color;
    uint32_t cr = clear & 31, cg = (clear >> 5) & 31, cb = (clear >> 10) & 31, ca = (clear >> 16) & 31;
    uint32_t clear_px = (cr * 255 / 31) | (cg * 255 / 31) << 8 | (cb * 255 / 31) << 16 | ca << 24;
    for (int i = 0; i < 256 * 192; i++) {
        out[i] = clear_px;
        s_depth[i] = 1e30f;
    }
    bool texturing = frame->disp3dcnt & 1;
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < frame->polygon_count; i++) {
            if (frame->polygons[i].hidden) continue;
            const GxPolygon *p = &frame->polygons[i];
            uint32_t alpha = (p->attr >> 16) & 31;
            bool fmt_alpha = ((p->teximage >> 26) & 7) == 1 || ((p->teximage >> 26) & 7) == 6;
            bool translucent = (alpha != 0 && alpha != 31) || fmt_alpha;
            if (translucent != (pass == 1)) continue;
            const uint32_t *tex = texturing ? texture(&map, f->vram, p->teximage, p->pltt) : NULL;
            V v[4], clipped[8];
            for (int k = 0; k < p->count; k++) {
                const GxVertex *gv = &frame->vertices[p->first + k];
                v[k] = (V) {gv->x / 4096.0f, gv->y / 4096.0f, gv->z / 4096.0f, gv->w / 4096.0f,
                            gv->s,           gv->t,           gv->r,           gv->g,
                            gv->b};
            }
            int n = clip_near(v, p->count, clipped);
            if (n < 3) continue;
            float x1 = p->viewport[0], y1 = p->viewport[1], x2 = p->viewport[2], y2 = p->viewport[3];
            for (int k = 0; k < n; k++) {
                V *c = &clipped[k];
                float w = c->w > 1e-6f ? c->w : 1e-6f;
                float nx = c->x / w, ny = c->y / w, nz = c->z / w;
                c->x = (nx + 1) * 0.5f * (x2 - x1 + 1) + x1;
                c->y = 192.0f - ((ny + 1) * 0.5f * (y2 - y1 + 1) + y1);
                c->z = nz;
                c->w = 1.0f / w;
            }
            for (int k = 1; k + 1 < n; k++) {
                raster_triangle(&clipped[0], &clipped[k], &clipped[k + 1], p, tex, out, pass == 1);
            }
        }
    }
}

// What one displayed frame needs: the 3D polygon list with its rendering state, and the 2D engines' registers and
// memories. Built by gx3d.c (polygons) and frame.c (snapshot), consumed by the renderers.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_MAX_POLYGONS 8192 // culled ones included (see GxPolygon.hidden)
#define GX_MAX_VERTICES 32768

typedef struct GxVertex {
    int32_t x, y, z, w; // clip space, 20.12 fixed point (before the viewport transform)
    int16_t s, t;       // texture coordinates, 12.4 texels
    uint8_t r, g, b;    // 0..31 (lit and clamped)
    uint8_t pad;
} GxVertex;

typedef struct GxPolygon {
    uint32_t first;      // index of the first vertex
    uint8_t count;       // 3 or 4
    uint8_t viewport[4]; // x1, y1, x2, y2 (DS screen coordinates, y up from the bottom as in VIEWPORT)
    uint8_t front;       // 1 when the polygon faces the viewer
    uint8_t hidden;      // culled or outside the view: not drawn, kept so that models have the same polygons every
                         // frame (frame interpolation pairs them up)
    uint32_t attr;       // POLYGON_ATTR latched at BEGIN_VTXS
    uint32_t teximage;   // TEXIMAGE_PARAM
    uint32_t pltt;       // PLTT_BASE
    uint32_t tag;        // place in its vertex block (BEGIN_VTXS), counting culled ones: identity for interpolation
    int32_t aspect;      // aspect ratio of its projection (20.12): 4:3 views keep their shape on a wide screen
    uint32_t obj;        // what the game was drawing (g_port_draw_id): identity of the object for interpolation
} GxPolygon;

typedef struct Gx3dFrame {
    GxVertex vertices[GX_MAX_VERTICES];
    GxPolygon polygons[GX_MAX_POLYGONS];
    uint32_t vertex_count, polygon_count;
    uint32_t swap_param; // SWAP_BUFFERS: bit 0 manual translucent sort, bit 1 W-buffering
    // Rendering registers latched at the swap.
    uint16_t disp3dcnt;
    uint32_t clear_color;
    uint16_t clear_depth;
    uint16_t clrimage_offset;
    uint32_t fog_color;
    uint16_t fog_offset;
    uint8_t fog_table[32];
    uint16_t edge_color[8];
    uint16_t toon_table[32];
    uint8_t alpha_ref;
    int32_t proj_aspect; // aspect ratio of the projection of the first polygon, 20.12 (0: none)
    uint64_t serial; // increases with every list
} Gx3dFrame;

// The 3D list being displayed (swapped in at the last VBlank).
const Gx3dFrame *gx3d_displayed(void);

#ifdef __cplusplus
}
#endif

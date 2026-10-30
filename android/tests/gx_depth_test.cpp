#include "ntr/gx.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {
void command(unsigned id, uint32_t value) {
    ntr::gx_write_port(0x04000400u + id * 4u, value);
}

// Overlapping screen-sized panels with independently controlled clip W and
// normalized Z. A constant projection Z recreates the title's depth collapse:
// the backdrop, wallpaper and dialog can have equal Z but distinct W.
void panel(int w, int clipZ, uint16_t color) {
    command(0x10, 0); // projection
    int32_t matrix[16]{};
    matrix[0] = matrix[5] = matrix[15] = w;
    matrix[14] = clipZ;
    for (auto value : matrix) command(0x16, static_cast<uint32_t>(value));
    command(0x10, 1); // position
    command(0x15, 0);
    command(0x2a, 0); // untextured
    command(0x29, 0x001f00c0); // opaque, both faces
    command(0x20, color);
    command(0x40, 1); // quad
    for (auto point : {0x1000f000u, 0xf000f000u, 0xf0001000u, 0x10001000u}) {
        command(0x23, point);
        command(0x23, 0);
    }
    command(0x41, 0);
}

void solid(const ntr::Framebuffer &frame, uint32_t expected) {
    for (const auto &row : frame.px)
        for (auto pixel : row) assert(pixel == expected);
}
}

int main() {
    ntr::Framebuffer frame{};
    for (bool reverse : {false, true}) {
        ntr::gx_reset();
        // The original title writes 2 to SWAP_BUFFERS before its first UI
        // geometry. Nearer panels must cover every pixel in either draw order.
        command(0x50, 2);
        auto near = [] { panel(4096, 0, 0x03e0); };
        auto far = [] { panel(8192, 0, 0x001f); };
        if (reverse) { near(); far(); } else { far(); near(); }
        frame = {};
        ntr::gx_render(frame);
        solid(frame, 0xff00ff00u);

        // A new host frame must retain the selected hardware depth mode.
        ntr::gx_begin_frame();
        far(); near();
        frame = {};
        ntr::gx_render(frame);
        solid(frame, 0xff00ff00u);

        // Explicit Z selection still compares Z, even when W says otherwise.
        ntr::gx_begin_frame();
        command(0x50, 0);
        auto nearZ = [] { panel(8192, -4096, 0x7c00); };
        auto farZ = [] { panel(4096, 2048, 0x001f); };
        if (reverse) { nearZ(); farZ(); } else { farZ(); nearZ(); }
        frame = {};
        ntr::gx_render(frame);
        solid(frame, 0xff0000ffu);
    }

    // A swap parameter belongs to subsequent geometry, not the already
    // assembled list. Exercise the packed FIFO as well as the direct port.
    ntr::gx_reset();
    panel(4096, 0, 0x001f);
    ntr::gx_write_fifo(0x50);
    ntr::gx_write_fifo(2);
    panel(4096, 0, 0x03e0);
    size_t count = 0;
    const auto *triangles = ntr::gx_polygons(count);
    assert(count == 4);
    assert(!triangles[0].w_buffer && !triangles[1].w_buffer);
    assert(triangles[2].w_buffer && triangles[3].w_buffer);
    std::puts("GX W/Z depth, coplanar menu coverage and swap latching passed.");
}

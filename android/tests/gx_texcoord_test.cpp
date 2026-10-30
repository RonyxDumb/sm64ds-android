#include "ntr/gx.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
void command(unsigned id, uint32_t value) {
    ntr::gx_write_port(0x04000400u + id * 4u, value);
}
void matrix(int xx, int yy, int zx, int zy, int tx, int ty) {
    command(0x10, 3);
    int32_t values[16]{};
    values[0]=xx*4096; values[5]=yy*4096;
    values[8]=zx*4096; values[9]=zy*4096;
    values[12]=tx*4096; values[13]=ty*4096; values[15]=4096;
    for (auto value : values) command(0x16, static_cast<uint32_t>(value));
    command(0x10, 1);
}
void uv(int u, int v) {
    command(0x22, uint16_t(u*16) | (uint32_t(uint16_t(v*16))<<16));
}
void vertex(int x, int y, int z) {
    command(0x23, uint16_t(x) | (uint32_t(uint16_t(y))<<16));
    command(0x23, uint16_t(z));
}
void normal(int x, int y, int z) {
    command(0x21, (uint32_t(x)&1023) | ((uint32_t(y)&1023)<<10) |
                  ((uint32_t(z)&1023)<<20));
}
void point(float u, float v, unsigned index=0) {
    size_t count=0;
    const auto *triangles=ntr::gx_polygons(count);
    assert(count==1);
    assert(triangles[0].v[index].u==u && triangles[0].v[index].v==v);
}
void begin(unsigned mode) {
    ntr::gx_reset();
    command(0x2a, mode<<30); // coordinate generation, no texel data needed
    command(0x40, 0);
}
void finish() {
    vertex(0,0,0); vertex(0,0,0); vertex(0,0,0);
    command(0x41,0);
}
}

int main() {
    begin(0);
    matrix(256,-256,128,64,300,400);
    uv(6,22); finish();
    point(6,22); // no transform

    begin(1);
    matrix(2,3,16,32,48,64);
    uv(6,22); finish();
    point(16,72); // TEXCOORD scale plus the two 1/16 constant lanes

    begin(2);
    // The title star's reflection uses a scale of 256 for a 32px texture.
    // A unit normal spans 16 texels, not 256. Translation comes from UV.
    matrix(256,-256,128,64,300,400);
    uv(6,22);
    normal(256,-128,64); // (1/2,-1/4,1/8) in signed 1.9 format
    vertex(0,0,0);
    uv(8,24); // does not replace the already latched NORMAL result
    vertex(0,0,0);
    normal(256,-128,64);
    vertex(0,0,0);
    command(0x41,0);
    point(15,26.5f,0); point(15,26.5f,1); point(17,28.5f,2);

    begin(2);
    matrix(8,0,0,0,0,0);
    uv(6,22); normal(-1,0,0); finish();
    point(5.9375f,22); // negative sub-texel result rounds down at 1/16

    begin(3);
    matrix(256,-256,128,64,300,400);
    uv(6,22);
    vertex(2048,-1024,512); // (1/2,-1/4,1/8) in signed 4.12 format
    vertex(-2048,1024,-512);
    vertex(0,0,0);
    command(0x41,0);
    point(15,26.5f,0); point(-3,17.5f,1); point(6,22,2);
    std::puts("GX texture units, all four source modes and coordinate latching passed.");
}

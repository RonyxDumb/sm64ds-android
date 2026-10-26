#pragma once
#include "ntr/ppu.h"
#include <GLES3/gl3.h>
namespace sm64ds {
class ScreenRenderer {
public:
    void create();
    // POWCNT1 bit 15 routes engine A to the physical top LCD when set.
    // Android places the physical top at left and the touch LCD at right.
    void draw(const ntr::Framebuffer &, const ntr::Framebuffer &, int width, int height,
              bool mainOnTop = true);
    void release();

private:
    GLuint program_ = 0, vao_ = 0, textures_[2]{};
};
}

#pragma once
#include "ntr/ppu.h"
#include <GLES3/gl3.h>
namespace sm64ds {
class ScreenRenderer {
public:
    void create();
    void draw(const ntr::Framebuffer &, const ntr::Framebuffer &, int width, int height);
    void release();

private:
    GLuint program_ = 0, vao_ = 0, textures_[2]{};
};
}

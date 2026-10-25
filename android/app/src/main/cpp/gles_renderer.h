#pragma once
#include "scene.h"
#include <GLES3/gl3.h>
namespace sm64ds {
class GlesRenderer {
public:
    void create(const Scene &scene); // current GL context, may be called after context loss
    void draw(const Scene &scene, int width, int height, float yaw, float zoom);
    void release(); // current GL context only
private:
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    std::vector<GLuint> textures_;
    GLint center_ = -1, radius_ = -1, aspect_ = -1, yaw_ = -1, zoom_ = -1, textured_ = -1;
};
}

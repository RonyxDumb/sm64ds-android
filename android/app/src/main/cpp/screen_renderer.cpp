#include "screen_renderer.h"
#include <algorithm>
#include <stdexcept>
namespace {
GLuint compile(GLenum kind, const char *source) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint valid = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &valid);
    if (!valid) {
        glDeleteShader(s);
        throw std::runtime_error("Native screen shader compilation failed.");
    }
    return s;
}
}
namespace sm64ds {
void ScreenRenderer::create() {
    const char *vertex = R"(#version 300 es
precision highp float;
out vec2 uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    uv = vec2(p.x, 1.0 - p.y);
})";
    const char *fragment = R"(#version 300 es
precision mediump float;
uniform sampler2D screen;
in vec2 uv;
out vec4 color;
void main() { color = texture(screen, uv).bgra; }
)";
    GLuint vs = 0, fs = 0;
    try {
        vs = compile(GL_VERTEX_SHADER, vertex);
        fs = compile(GL_FRAGMENT_SHADER, fragment);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        vs = fs = 0;
        GLint valid = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &valid);
        if (!valid)
            throw std::runtime_error("Native screen program link failed.");
        glGenVertexArrays(1, &vao_);
        glGenTextures(2, textures_);
        for (auto texture : textures_) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, ntr::SCREEN_W, ntr::SCREEN_H);
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("Native screen texture allocation failed.");
    } catch (...) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        release();
        throw;
    }
}
void ScreenRenderer::draw(const ntr::Framebuffer &main, const ntr::Framebuffer &sub, int width,
                          int height) {
    if (width <= 0 || height <= 0)
        return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program_, "screen"), 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const ntr::Framebuffer *frames[] = {&main, &sub};
    // Boot has two interface cards. Stage rendering will own the full-screen
    // viewport and minimap policy once that scene is linked.
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ntr::SCREEN_W, ntr::SCREEN_H, GL_RGBA,
                        GL_UNSIGNED_BYTE, frames[i]->px);
        int half = width / 2, w = std::min(half, height * 4 / 3), h = w * 3 / 4;
        glViewport(i * half + (half - w) / 2, (height - h) / 2, w, h);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}
void ScreenRenderer::release() {
    glDeleteTextures(2, textures_);
    textures_[0] = textures_[1] = 0;
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    if (program_)
        glDeleteProgram(program_);
    vao_ = program_ = 0;
}
}

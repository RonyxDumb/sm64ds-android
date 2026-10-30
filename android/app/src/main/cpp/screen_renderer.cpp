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
uniform vec2 cropScale;
in vec2 uv;
out vec4 color;
void main() {
    vec2 mappedUV = (uv - vec2(0.5)) * cropScale + vec2(0.5);
    color = texture(screen, mappedUV).bgra;
}
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
                          int height, bool mainOnTop) {
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
    // The DS routes engines to PHYSICAL LCDs with POWCNT1 bit 15. Engine A
    // is NOT inherently the top screen. Earlier builds always showed A on
    // the left, so when the game placed B on top it displayed the menu on
    // the wrong side and mapped the stylus onto the wrong image.
    const ntr::Framebuffer *frames[] = {
        mainOnTop ? &main : &sub, mainOnTop ? &sub : &main
    };
    // Preserve physical LCD routing: POWCNT1 bit 15 selects which engine
    // feeds the upper display. The lower (stylus) display is always the inset.
    const ntr::Framebuffer *top = mainOnTop ? &main : &sub;
    const ntr::Framebuffer *bottom = mainOnTop ? &sub : &main;
    const GLint crop = glGetUniformLocation(program_, "cropScale");

    // Fill the device surface while preserving the DS picture's 4:3 aspect:
    // crop its edges instead of stretching characters or backgrounds.
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ntr::SCREEN_W, ntr::SCREEN_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, top->px);
    const float screenAspect = static_cast<float>(width) / height;
    const float nativeAspect = static_cast<float>(ntr::SCREEN_W) / ntr::SCREEN_H;
    const float cropX = screenAspect < nativeAspect ? screenAspect / nativeAspect : 1.0f;
    const float cropY = screenAspect > nativeAspect ? nativeAspect / screenAspect : 1.0f;
    glUniform2f(crop, cropX, cropY);
    glViewport(0, 0, width, height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Picture-in-picture touch LCD: 24% of device width, bottom-right.
    // Keep its original 256:192 ratio independently of the host display.
    const int margin = std::max(4, width / 100);
    const int insetWidth = std::max(1, std::min(width - 2 * margin, width * 24 / 100));
    const int insetHeight = std::max(1, insetWidth * ntr::SCREEN_H / ntr::SCREEN_W);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ntr::SCREEN_W, ntr::SCREEN_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, bottom->px);
    glUniform2f(crop, 1.0f, 1.0f);
    glViewport(width - margin - insetWidth, margin, insetWidth, insetHeight);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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

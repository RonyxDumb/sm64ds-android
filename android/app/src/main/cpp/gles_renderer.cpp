#include "gles_renderer.h"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
namespace sm64ds {
namespace {
GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader: ") + log);
    }
    return s;
}
const char *vertex = R"(#version 300 es
    precision highp float;
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 color;
uniform vec3 center;
uniform float radius, aspect, yaw, zoom;
out vec2 uv;
out vec4 tint;
void main() {
    vec3 p = (position - center) / radius;
    float c = cos(yaw), s = sin(yaw);
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    // Perspective with fixed vertical FOV; adapt horizontal FOV to display aspect.
    float z = 3.2 / zoom - p.z;
    float f = 2.41421356;
    gl_Position = vec4(p.x * f / aspect, p.y * f,
                       (10.0 + 0.1) / (10.0 - 0.1) * z - 2.0 * 10.0 * 0.1 / (10.0 - 0.1), z);
    uv = texcoord;
    tint = color;
})";
const char *fragment = R"(#version 300 es
precision mediump float;
in vec2 uv;
in vec4 tint;
uniform sampler2D image;
uniform bool textured;
out vec4 result;
void main() {
    result = tint * (textured ? texture(image, uv) : vec4(1.0));
    if (result.a < 0.01)
        discard;
}
)";
}
void GlesRenderer::create(const Scene &scene) {
    GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = 0;
    try {
        fs = shader(GL_FRAGMENT_SHADER, fragment);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok)
        throw std::runtime_error("OpenGL ES program link failed.");
    center_ = glGetUniformLocation(program_, "center");
    radius_ = glGetUniformLocation(program_, "radius");
    aspect_ = glGetUniformLocation(program_, "aspect");
    yaw_ = glGetUniformLocation(program_, "yaw");
    zoom_ = glGetUniformLocation(program_, "zoom");
    textured_ = glGetUniformLocation(program_, "textured");
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(scene.vertices.size() * sizeof(Vertex)),
                 scene.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, r)));
    textures_.assign(scene.textures.size(), 0);
    glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    for (size_t i = 0; i < textures_.size(); ++i) {
        const auto &t = scene.textures[i];
        if (t.rgba.empty())
            continue;
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        t.wrapS == 2   ? GL_MIRRORED_REPEAT
                        : t.wrapS == 1 ? GL_REPEAT
                                       : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        t.wrapT == 2   ? GL_MIRRORED_REPEAT
                        : t.wrapT == 1 ? GL_REPEAT
                                       : GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     t.rgba.data());
    }
    if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GPU asset upload failed.");
}
void GlesRenderer::draw(const Scene &scene, int width, int height, float yaw, float zoom) {
    if (width <= 0 || height <= 0)
        return;
    glViewport(0, 0, width, height);
    glClearColor(.015f, .025f, .04f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glUniform3fv(center_, 1, scene.center.data());
    glUniform1f(radius_, scene.radius);
    glUniform1f(aspect_, float(width) / height);
    glUniform1f(yaw_, yaw);
    glUniform1f(zoom_, std::clamp(zoom, .5f, 1.5f));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program_, "image"), 0);
    for (const auto &b : scene.batches) {
        bool textured = b.texture >= 0 && !scene.textures[b.texture].rgba.empty();
        glUniform1i(textured_, textured);
        glBindTexture(GL_TEXTURE_2D, textured ? textures_[b.texture] : 0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(b.first), static_cast<GLsizei>(b.count));
    }
}
void GlesRenderer::release() {
    if (!textures_.empty())
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    textures_.clear();
    if (vbo_)
        glDeleteBuffers(1, &vbo_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    if (program_)
        glDeleteProgram(program_);
    vbo_ = vao_ = program_ = 0;
}
}

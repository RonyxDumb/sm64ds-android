#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace sm64ds {
struct Vertex {
    float x, y, z, u, v;
    uint8_t r, g, b, a;
};
struct Texture {
    int width, height;
    std::vector<uint8_t> rgba;
    int wrapS = 0, wrapT = 0;
};
struct Batch {
    uint32_t first, count;
    int texture;
};
struct Scene {
    std::vector<Vertex> vertices;
    std::vector<Texture> textures;
    std::vector<Batch> batches;
    std::array<float, 3> center{};
    float radius = 1;
};
// Caller serializes access to the existing ntr global filesystem/GX state.
// Loads original ROM assets; no guest code is executed.
Scene load_scene(const char *path);
}

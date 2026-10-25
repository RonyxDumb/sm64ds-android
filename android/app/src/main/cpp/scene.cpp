#include "scene.h"
#include "ntr/bmd.h"
#include "ntr/fs.h"
#include "ntr/gx.h"
#include "runtime/resources.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace sm64ds {
Scene load_scene(const char *path, Resources *resources) {
    struct Cleanup {
        bool ownsMount;
        ~Cleanup() {
            ntr::bmd_set_animation(nullptr);
            ntr::gx_reset();
            if (ownsMount) ntr::fs_unmount();
        }
    } cleanup{!resources};
    if (!resources && !ntr::fs_mount_rom(path))
        throw std::runtime_error("Cannot mount saved ROM.");
    ntr::BmdModel model;
    std::vector<uint8_t> pose;
    if (resources) {
        if (!ntr::bmd_parse(resources->load("data/DSMT/face_demo_mario.bmd"), model))
            throw std::runtime_error("Cannot parse the original Mario model.");
        pose = resources->load("data/DSMT/face_demo_wait_mario.bca");
    } else if (!ntr::bmd_load_path("data/DSMT/face_demo_mario.bmd", model) ||
               !ntr::bca_load_path("data/DSMT/face_demo_wait_mario.bca", pose)) {
        throw std::runtime_error("Cannot load the original Mario model/pose.");
    }
    ntr::gx_reset();
    ntr::gx_set_light(0, -.4f, -.6f, -.7f, 0x7fff);
    ntr::gx_enable_lights(1);
    ntr::bmd_set_animation(&pose);
    ntr::bmd_submit(model);
    size_t count = 0;
    const auto *tri = ntr::gx_polygons(count);
    if (!count || count > 100000)
        throw std::runtime_error("Invalid native geometry output.");
    Scene scene;
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < count; ++i) {
        int texture = -1;
        for (size_t t = 0; t < model.textures.size(); ++t)
            if (tri[i].tex && tri[i].tex == model.textures[t].rgba.data())
                texture = static_cast<int>(t);
        if (scene.batches.empty() || scene.batches.back().texture != texture)
            scene.batches.push_back({static_cast<uint32_t>(scene.vertices.size()), 0, texture});
        for (const auto &v : tri[i].v) {
            Vertex out{v.x / 128.f - 1.f,
                       1.f - v.y / 96.f,
                       2.f * v.z - 1.f,
                       v.u,
                       v.v,
                       static_cast<uint8_t>(v.color >> 16),
                       static_cast<uint8_t>(v.color >> 8),
                       static_cast<uint8_t>(v.color),
                       static_cast<uint8_t>(v.color >> 24)};
            const float pos[] = {out.x, out.y, out.z};
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(pos[k]))
                    throw std::runtime_error("Non-finite model vertex.");
                lo[k] = std::min(lo[k], pos[k]);
                hi[k] = std::max(hi[k], pos[k]);
            }
            if (texture >= 0) {
                out.u /= tri[i].tw;
                out.v /= tri[i].th;
            }
            scene.vertices.push_back(out);
            scene.batches.back().count++;
        }
    }
    for (int k = 0; k < 3; ++k)
        scene.center[k] = (lo[k] + hi[k]) * .5f;
    float extent = 0;
    for (int k = 0; k < 3; ++k)
        extent += (hi[k] - lo[k]) * (hi[k] - lo[k]);
    scene.radius = std::max(.01f, std::sqrt(extent) * .5f);
    for (const auto &t : model.textures) {
        Texture out{t.width, t.height, {}};
        out.wrapS = (t.texture_param & (1u << 16)) ? ((t.texture_param & (1u << 18)) ? 2 : 1) : 0;
        out.wrapT = (t.texture_param & (1u << 17)) ? ((t.texture_param & (1u << 19)) ? 2 : 1) : 0;
        for (uint32_t pixel : t.rgba) {
            out.rgba.push_back(pixel >> 16);
            out.rgba.push_back(pixel >> 8);
            out.rgba.push_back(pixel);
            out.rgba.push_back(pixel >> 24);
        }
        scene.textures.push_back(std::move(out));
    }
    return scene;
}
}

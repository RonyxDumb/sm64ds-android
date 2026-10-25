#pragma once
#include "scene.h"
#include "gles_renderer.h"
#include "android_audio.h"
#include <memory>
#include <mutex>
namespace sm64ds {
class Session {
public:
    explicit Session(const char *path);
    void surface() {
        renderer_.create(scene_);
    }
    void frame(int width, int height, float dt, uint32_t buttons, float x, float y, int touchX,
               int touchY, bool audioEnabled);
    void release(bool contextValid = true) {
        audio_->close();
        if (contextValid)
            renderer_.release();
    }
    size_t triangles() const {
        return scene_.vertices.size() / 3;
    }
    size_t sounds() const {
        return audio_->clip_count();
    }
    bool audio_running() const {
        return audioReady_;
    }

private:
    Scene scene_;
    GlesRenderer renderer_;
    std::unique_ptr<AndroidAudio> audio_;
    uint32_t previous_ = 0, sample_ = 0;
    float yaw_ = 0, zoom_ = 1, audioRetry_ = 0;
    bool audioReady_ = false;
};
std::mutex &platform_mutex();
}

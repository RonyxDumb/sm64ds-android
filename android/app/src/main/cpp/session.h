#pragma once
#include "screen_renderer.h"
#include "android_audio.h"
#include "runtime/save_store.h"
#include "runtime/resources.h"
#include "runtime/game_program.h"
#include <memory>
#include <mutex>
namespace sm64ds {
class Session {
public:
    explicit Session(const char *path);
    void surface() {
        renderer_.create();
    }
    void frame(int width, int height, float dt, uint32_t buttons, float x, float y, int touchX,
               int touchY, bool audioEnabled);
    void release(bool contextValid = true) {
        audio_->close();
        if (contextValid)
            renderer_.release();
    }
    bool audio_running() const {
        return audioReady_;
    }

private:
    std::unique_ptr<Resources> resources_;
    ScreenRenderer renderer_;
    std::unique_ptr<AndroidAudio> audio_;
    std::unique_ptr<SaveStore> saves_;
    std::unique_ptr<GameProgram> game_;
    float audioRetry_ = 0;
    bool audioReady_ = false;
};
std::mutex &platform_mutex();
}

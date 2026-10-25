#include <mutex>
#include "session.h"
#include "ntr/fs.h"
#include <algorithm>
#include <stdexcept>
namespace sm64ds {
std::mutex &platform_mutex() {
    static std::mutex lock;
    return lock;
}
Session::Session(const char *path) {
    std::lock_guard<std::mutex> guard(platform_mutex());
    resources_ = std::make_unique<Resources>(path);
    scene_ = load_scene(path, resources_.get());
    audio_ = std::make_unique<AndroidAudio>(load_audio_clips());
    const std::string rom(path);
    const auto slash = rom.find_last_of('/');
    if (slash == std::string::npos)
        throw std::runtime_error("The private ROM path must be absolute.");
    saves_ = std::make_unique<SaveStore>(rom.substr(0, slash + 1) + "game.sav");
}
void Session::frame(int width, int height, float dt, uint32_t buttons, float x, float y, int touchX,
                    int touchY, bool audioEnabled) {
    // Diagnostic scene only: do not fabricate player movement before the real
    // actor/scene runtime is linked. The entire DS button/touch snapshot arrives
    // here, ready for the recovered input seam.
    input_.update(buttons, x, y, touchX, touchY);
    dt = std::clamp(dt, 0.f, .05f);
    yaw_ += (x + ((buttons & (1u << 17)) ? 1.f : 0.f) - ((buttons & (1u << 16)) ? 1.f : 0.f)) * dt *
            1.8f;
    zoom_ = std::clamp(zoom_ + y * dt * .4f, .5f, 1.5f);
    uint32_t pressed = buttons & ~previous_;
    previous_ = buttons;
    if (pressed & 2)
        sample_ = (sample_ + 1) % static_cast<uint32_t>(audio_->clip_count());
    if (!audioEnabled) {
        if (audioReady_)
            audio_->close();
        audioReady_ = false;
    }
    if (audioEnabled && (pressed & 1)) {
        audioReady_ = audio_->start();
        audioRetry_ = 1;
        if (audioReady_)
            audio_->sample(sample_);
    }
    if (audioEnabled && (pressed & 1024)) {
        audioReady_ = audio_->start();
        if (audioReady_)
            audio_->music();
    }
    if (pressed & 2048)
        audio_->stop_music();
    audioRetry_ -= dt;
    if (audioEnabled && audio_->disconnected() && audioRetry_ <= 0) {
        audioReady_ = audio_->start();
        audioRetry_ = 1;
    } // never reopen from the callback
    renderer_.draw(scene_, width, height, yaw_, zoom_);
}
}

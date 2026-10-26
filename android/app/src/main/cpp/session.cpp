#include "session.h"
#include <stdexcept>
namespace sm64ds {
std::mutex &platform_mutex() { static std::mutex lock; return lock; }
Session::Session(const char *path) {
    std::lock_guard<std::mutex> guard(platform_mutex());
    resources_ = std::make_unique<Resources>(path);
    audio_ = std::make_unique<AndroidAudio>();
    const std::string rom(path);
    auto slash = rom.find_last_of('/');
    if (slash == std::string::npos) throw std::runtime_error("Private ROM path must be absolute.");
    saves_ = std::make_unique<SaveStore>(rom.substr(0, slash + 1) + "game.sav");
    game_ = std::make_unique<GameProgram>(path, [this](uint32_t a, uint32_t b) { return audio_->effect(a, b); });
}
void Session::frame(int width, int height, float dt, uint32_t buttons, float x, float y, int tx, int ty, bool enabled) {
    if (!enabled) { if (audioReady_) audio_->close(); audioReady_ = false; }
    audioRetry_ -= dt;
    if (enabled && (!audioReady_ || audio_->disconnected()) && audioRetry_ <= 0) {
        audioReady_ = audio_->start(); audioRetry_ = 1;
    }
    game_->frame(dt, buttons, x, y, tx, ty);
    renderer_.draw(game_->main_screen(), game_->sub_screen(), width, height);
}
}

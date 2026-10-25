#pragma once
#include "audio_core.h"
#include "pc_audio/stream.h"
#include <aaudio/AAudio.h>
namespace sm64ds {
class AndroidAudio {
public:
    explicit AndroidAudio(std::vector<Clip> clips) : mixer_(std::move(clips)) {}
    ~AndroidAudio() {
        close();
    }
    void music() {
        music_.sequence(58);
    }
    void stop_music() {
        music_.stop();
    }
    bool start(); // owner GL thread only
    void close();
    void sample(uint32_t id) {
        mixer_.trigger({static_cast<uint32_t>(id % mixer_.clip_count()), .4f, 0, 1});
    }
    size_t clip_count() const {
        return mixer_.clip_count();
    }
    bool disconnected() const {
        return disconnected_.load(std::memory_order_relaxed);
    }

private:
    Mixer mixer_;
    MusicStream music_;
    AAudioStream *stream_ = nullptr;
    std::atomic<bool> disconnected_{false};
    static aaudio_data_callback_result_t callback(AAudioStream *, void *, void *, int32_t);
    static void error(AAudioStream *, void *, aaudio_result_t);
};
}

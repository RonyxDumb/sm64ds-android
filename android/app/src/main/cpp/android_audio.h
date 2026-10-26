#pragma once
#include "pc_audio/stream.h"
#include <aaudio/AAudio.h>
namespace sm64ds {
class AndroidAudio {
public:
    AndroidAudio() = default;
    ~AndroidAudio() {
        close();
    }
    bool effect(uint32_t archive, uint32_t entry) {
        return archive <= INT32_MAX && entry <= INT32_MAX && music_.effect(static_cast<int>(archive), static_cast<int>(entry));
    }
    bool start(); // owner GL thread only
    void close();
    bool disconnected() const {
        return disconnected_.load(std::memory_order_relaxed);
    }

private:
    MusicStream music_;
    AAudioStream *stream_ = nullptr;
    std::atomic<bool> disconnected_{false};
    static aaudio_data_callback_result_t callback(AAudioStream *, void *, void *, int32_t);
    static void error(AAudioStream *, void *, aaudio_result_t);
};
}

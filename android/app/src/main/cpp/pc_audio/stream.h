#pragma once
#include "archive.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace sm64ds {
// PC sequencer/mixer run off the audio clock on a producer thread. The AAudio
// callback only resamples a bounded PCM ring; it never executes SSEQ opcodes.
class MusicStream {
public:
    MusicStream(); // mounted ntr filesystem; owns the upstream global engine
    ~MusicStream();
    bool sequence(int id);
    bool effect(int archive, int entry);
    bool stop();
    void pause(bool value);
    void mix(float *stereo, size_t frames, uint32_t outputRate); // callback consumer
    uint32_t underruns() const {
        return underruns_.load();
    }

private:
    struct Command {
        int kind, a, b;
    };
    std::array<Command, 32> commands_{};
    std::atomic<uint32_t> commandRead_{0}, commandWrite_{0};
    static constexpr uint32_t Capacity = 2048;
    std::array<sd_s16, Capacity * 2> pcm_{};
    std::atomic<uint32_t> read_{0}, write_{0}, underruns_{0};
    std::atomic<bool> quit_{false}, paused_{true};
    std::mutex waitMutex_;
    std::condition_variable ready_;
    std::thread producer_;
    std::unique_lock<std::mutex> lease_;
    double fraction_ = 0;
    bool enqueue(Command command);
    void produce();
};
}

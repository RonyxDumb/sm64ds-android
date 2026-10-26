#pragma once
#include "archive.h"
#include "runtime/sound_request.h"
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
    int native(const SoundRequest &request);
    void pause(bool value);
    void mix(float *stereo, size_t frames, uint32_t outputRate); // callback consumer
    uint32_t underruns() const {
        return underruns_.load();
    }

private:
    struct Command {
        int kind, a, b;
        SoundRequest native{};
    };
    std::array<Command, 128> commands_{};
    std::array<std::atomic<uint32_t>,16> acknowledged_{};
    std::atomic<uint32_t> active_{0};
    std::atomic<int> mode_{0};
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

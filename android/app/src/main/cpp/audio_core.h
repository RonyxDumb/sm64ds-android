#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
namespace sm64ds {
struct Clip {
    std::vector<float> pcm;
    double rate = 0;
    size_t loopStart = 0;
    bool loop = false;
};
Clip decode_wave(const uint8_t *data, size_t size);
std::vector<Clip> load_audio_clips(); // mounted ntr filesystem, first 32 short one-shots
struct Note {
    uint32_t clip = 0;
    float gain = .5f, pan = 0, pitch = 1;
};
class Mixer {
public:
    explicit Mixer(std::vector<Clip> clips) : clips_(std::move(clips)) {}
    bool trigger(Note note);                                  // single producer, no allocations
    void render(float *stereo, size_t frames, uint32_t rate); // one real-time consumer
    void reset(); // only when the audio callback has stopped
    size_t clip_count() const {
        return clips_.size();
    }

private:
    struct Voice {
        const Clip *clip = nullptr;
        double at = 0;
        float gain = 0, pan = 0, pitch = 1;
    };
    std::vector<Clip> clips_;
    std::array<Voice, 16> voices_{};
    std::array<Note, 64> queue_{};
    std::atomic<uint32_t> read_{0}, write_{0};
    size_t nextVoice_ = 0;
};
}

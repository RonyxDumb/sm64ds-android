#include "audio_core.h"
#include "ntr/fs.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace sm64ds {
namespace {
uint16_t u16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}
uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("Invalid sound archive/wave bounds.");
}
const int steps[] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int index_delta[] = {-1, -1, -1, -1, 2, 4, 6, 8};
}
Clip decode_wave(const uint8_t *p, size_t size) {
    require(p && size >= 12);
    const uint64_t words = uint64_t(u16(p + 6)) + u32(p + 8);
    require(words && words <= 1024 * 1024 && words * 4 <= size - 12);
    const size_t bytes = static_cast<size_t>(words * 4);
    Clip clip;
    // Nitro programs the hardware timer; the nominal sample-rate field is rounded.
    clip.rate = u16(p + 4) ? 16756991.0 / u16(p + 4) : u16(p + 2);
    clip.loop = p[1] != 0;
    require(clip.rate >= 1000 && clip.rate <= 65535 && p[0] <= 2);
    const size_t offset = size_t(u16(p + 6)) * 4;
    p += 12;
    if (p[-12] == 0) {
        clip.pcm.reserve(bytes);
        for (size_t i = 0; i < bytes; ++i)
            clip.pcm.push_back(static_cast<int8_t>(p[i]) / 128.f);
        clip.loopStart = offset;
    } else if (p[-12] == 1) {
        clip.pcm.reserve(bytes / 2);
        for (size_t i = 0; i < bytes; i += 2)
            clip.pcm.push_back(static_cast<int16_t>(u16(p + i)) / 32768.f);
        clip.loopStart = offset / 2;
    } else {
        require(bytes >= 4 && p[2] <= 88 && p[3] == 0);
        int predictor = static_cast<int16_t>(u16(p)), index = p[2];
        clip.pcm.reserve((bytes - 4) * 2 + 1);
        clip.pcm.push_back(predictor / 32768.f);
        for (size_t i = 4; i < bytes; ++i)
            for (int shift : {0, 4}) {
                const int nibble = (p[i] >> shift) & 15, step = steps[index];
                const int diff = (step >> 3) + ((nibble & 1) ? step >> 2 : 0) +
                                 ((nibble & 2) ? step >> 1 : 0) + ((nibble & 4) ? step : 0);
                predictor = std::clamp(predictor + ((nibble & 8) ? -diff : diff), -32767, 32767);
                index = std::clamp(index + index_delta[nibble & 7], 0, 88);
                clip.pcm.push_back(predictor / 32768.f);
            }
        require(!clip.loop || offset >= 4);
        clip.loopStart = offset >= 4 ? offset * 2 - 7 : 0;
    }
    require(!clip.pcm.empty() && (!clip.loop || clip.loopStart < clip.pcm.size()));
    return clip;
}
std::vector<Clip> load_audio_clips() {
    int id = ntr::fs_id_by_path("data/sound_data.sdat");
    int size = ntr::fs_size(id);
    require(size >= 0x40 && size <= 32 * 1024 * 1024);
    std::vector<uint8_t> b(size);
    require(ntr::fs_read(id, b.data(), size) == size);
    require(!std::memcmp(b.data(), "SDAT", 4));
    const size_t fat = u32(b.data() + 0x20);
    require(fat <= b.size() && b.size() - fat >= 12 && !std::memcmp(b.data() + fat, "FAT ", 4));
    uint32_t count = u32(b.data() + fat + 8);
    require(count < 65536 && uint64_t(count) * 16 <= b.size() - fat - 12);
    std::vector<Clip> clips;
    for (uint32_t i = 0; i < count && clips.size() < 32; ++i) {
        const uint8_t *record = b.data() + fat + 12 + i * 16;
        const size_t off = u32(record), len = u32(record + 4);
        require(off <= b.size() && len <= b.size() - off);
        if (len < 0x3c || std::memcmp(b.data() + off, "SWAR", 4))
            continue;
        const uint8_t *archive = b.data() + off;
        uint32_t waves = u32(archive + 0x38);
        require(waves < 65536 && uint64_t(waves) * 4 <= len - 0x3c);
        for (uint32_t w = 0; w < waves && clips.size() < 32; ++w) {
            size_t start = u32(archive + 0x3c + w * 4);
            require(start <= len && len - start >= 12);
            if (archive[start + 1])
                continue; // instrument loops need the SSEQ envelope engine
            Clip clip = decode_wave(archive + start, len - start);
            if (double(clip.pcm.size()) >= clip.rate / 20 &&
                double(clip.pcm.size()) <= clip.rate * 4)
                clips.push_back(std::move(clip));
        }
    }
    require(!clips.empty());
    return clips;
}
void Mixer::reset() {
    for (auto &voice : voices_)
        voice = Voice{};
    read_.store(write_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}
bool Mixer::trigger(Note note) {
    if (note.clip >= clips_.size() || !std::isfinite(note.gain) || !std::isfinite(note.pan) ||
        !std::isfinite(note.pitch) || note.pitch <= 0 || note.pitch > 8)
        return false;
    uint32_t w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == queue_.size())
        return false;
    queue_[w % queue_.size()] = note;
    write_.store(w + 1, std::memory_order_release);
    return true;
}
void Mixer::render(float *out, size_t frames, uint32_t rate) {
    std::fill(out, out + frames * 2, 0.f);
    if (!rate)
        return;
    uint32_t r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
    while (r != w) {
        Note n = queue_[r % queue_.size()];
        size_t slot = nextVoice_++ % voices_.size();
        for (size_t i = 0; i < voices_.size(); ++i)
            if (!voices_[i].clip) {
                slot = i;
                break;
            }
        voices_[slot] = {&clips_[n.clip], 0, std::clamp(n.gain, 0.f, 1.f),
                         std::clamp(n.pan, -1.f, 1.f), n.pitch};
        ++r;
    }
    read_.store(r, std::memory_order_release);
    for (auto &v : voices_)
        if (v.clip) {
            const Clip &c = *v.clip;
            double step = double(c.rate) * v.pitch / rate;
            for (size_t f = 0; f < frames; ++f) {
                if (v.at >= c.pcm.size()) {
                    if (c.loop)
                        v.at =
                            c.loopStart + std::fmod(v.at - c.loopStart, c.pcm.size() - c.loopStart);
                    else {
                        v.clip = nullptr;
                        break;
                    }
                }
                size_t a = static_cast<size_t>(v.at), b = a + 1;
                if (b >= c.pcm.size())
                    b = c.loop ? c.loopStart : a;
                float sample = (c.pcm[a] + (c.pcm[b] - c.pcm[a]) * float(v.at - a)) * v.gain;
                out[f * 2] += sample * std::min(1.f, 1.f - v.pan);
                out[f * 2 + 1] += sample * std::min(1.f, 1.f + v.pan);
                v.at += step;
            }
        }
    for (size_t i = 0; i < frames * 2; ++i)
        out[i] = std::clamp(out[i], -1.f, 1.f);
}
}

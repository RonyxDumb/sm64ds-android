#include "stream.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace sm64ds {
namespace {
std::mutex engineOwner;
}
MusicStream::MusicStream() : lease_(engineOwner, std::try_to_lock) {
    if (!lease_.owns_lock())
        throw std::runtime_error("Another native sequencer session is still active.");
    try {
        pc_archive_load();
        producer_ = std::thread(&MusicStream::produce, this);
    } catch (...) {
        pc_archive_close();
        throw;
    }
}
MusicStream::~MusicStream() {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        quit_.store(true);
    }
    ready_.notify_one();
    if (producer_.joinable())
        producer_.join();
    pc_archive_close();
}
bool MusicStream::enqueue(Command command) {
    uint32_t w = commandWrite_.load(std::memory_order_relaxed);
    if (w - commandRead_.load(std::memory_order_acquire) == commands_.size())
        return false;
    commands_[w % commands_.size()] = command;
    commandWrite_.store(w + 1, std::memory_order_release);
    ready_.notify_one();
    return true;
}
bool MusicStream::sequence(int id) {
    return enqueue({0, id, 0});
}
bool MusicStream::effect(int archive, int entry) {
    return enqueue({1, archive, entry});
}
bool MusicStream::stop() {
    return enqueue({2, 0, 0});
}
void MusicStream::pause(bool value) {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        paused_.store(value);
    }
    ready_.notify_one();
}
void MusicStream::produce() {
    std::array<sd_s16, 256 * 2> block{};
    while (!quit_.load()) {
        if (paused_.load()) {
            std::unique_lock<std::mutex> lock(waitMutex_);
            ready_.wait(lock, [this] { return quit_.load() || !paused_.load(); });
            continue;
        }
        uint32_t r = commandRead_.load(std::memory_order_relaxed),
                 w = commandWrite_.load(std::memory_order_acquire);
        while (r != w) {
            const Command command = commands_[r % commands_.size()];
            if (command.kind == 0)
                pc_start_sequence(0, command.a);
            else if (command.kind == 1)
                pc_start_effect(1, command.a, command.b);
            else
                for (int player = 0; player < 16; player++)
                    sd_seq_stop(player);
            ++r;
        }
        commandRead_.store(r, std::memory_order_release);
        uint32_t write = write_.load(std::memory_order_relaxed),
                 available = write - read_.load(std::memory_order_acquire);
        if (available >= 1024) {
            std::unique_lock<std::mutex> lock(waitMutex_);
            ready_.wait_for(lock, std::chrono::milliseconds(2));
            continue;
        }
        sd_mix_render(block.data(), 256);
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t slot = (write + i) % Capacity;
            pcm_[slot * 2] = block[i * 2];
            pcm_[slot * 2 + 1] = block[i * 2 + 1];
        }
        write_.store(write + 256, std::memory_order_release);
    }
}
void MusicStream::mix(float *out, size_t frames, uint32_t rate) {
    if (!rate)
        return;
    uint32_t r = read_.load(std::memory_order_relaxed);
    double step = double(SD_MIX_RATE) / rate;
    for (size_t frame = 0; frame < frames; frame++) {
        if (write_.load(std::memory_order_acquire) - r <
            std::max(2u, static_cast<uint32_t>(fraction_ + step))) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        uint32_t a = r % Capacity, b = (r + 1) % Capacity;
        for (int ch = 0; ch < 2; ch++) {
            float value = float(pcm_[a * 2 + ch]) +
                          (float(pcm_[b * 2 + ch]) - pcm_[a * 2 + ch]) * float(fraction_);
            out[frame * 2 + ch] =
                std::clamp(out[frame * 2 + ch] + value * (.3f / 32768.f), -1.f, 1.f);
        }
        fraction_ += step;
        uint32_t advance = static_cast<uint32_t>(fraction_);
        fraction_ -= advance;
        r += advance;
    }
    read_.store(r, std::memory_order_release);
}
}

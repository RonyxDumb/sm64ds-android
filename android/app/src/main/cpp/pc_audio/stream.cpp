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
int MusicStream::native(const SoundRequest &request) {
    if (request.player<0 || request.player>=16) return 0;
    if (request.op==SoundOp::Status) {
        if (acknowledged_[request.player].load(std::memory_order_acquire)!=request.token) return 1;
        return (active_.load(std::memory_order_acquire)&(1u<<request.player))?1:0;
    }
    if (request.op==SoundOp::Effect && !pc_effect_exists(request.a,request.b)) return 0;
    if (request.op==SoundOp::TrackParam && request.b!=9 && request.b!=0x0a && request.b!=0x0c) return 0;
    if (request.op==SoundOp::PlayerParam && request.a!=4 && request.a!=6) return 0;
    if (request.op==SoundOp::Mode) {
        if (request.a<0 || request.a>1) return 0;
        mode_.store(request.a,std::memory_order_release); return 1;
    }
    return enqueue({3,0,0,request})?1:0;
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
    struct Fade { float level=127,target=127,rate=0; bool stop=false; };
    std::array<Fade,16> fades{};
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
            else if (command.kind==2)
                for (int player = 0; player < 16; player++)
                    sd_seq_stop(player);
            else if (command.kind==3) {
                const auto &n=command.native; auto &fade=fades[n.player];
                switch (n.op) {
                case SoundOp::Effect:
                    pc_start_effect(n.player,n.a,n.b); fade=Fade{};
                    active_.store(sd_seq_player_mask(),std::memory_order_release);
                    acknowledged_[n.player].store(n.token,std::memory_order_release);
                    break;
                case SoundOp::Music:
                    pc_start_sequence(n.player,n.a); fade=Fade{}; break;
                case SoundOp::Stop:
                    if (n.a<=0 || fade.level<=0) { sd_seq_stop(n.player); fade.rate=0; }
                    else { fade.target=0; fade.rate=-fade.level*59.8261f/n.a; fade.stop=true; }
                    break;
                case SoundOp::Fade:
                    fade.target=static_cast<float>(std::clamp(n.a,0,127)); fade.stop=false;
                    if (n.b<=0) {
                        fade.level=fade.target; fade.rate=0;
                        sd_seq_set_volume_db10(n.player,sd_cnv_vol(static_cast<int>(fade.level)));
                    } else fade.rate=(fade.target-fade.level)*59.8261f/n.b;
                    break;
                case SoundOp::PlayerParam:
                    if (n.a==4) sd_seq_set_volume(n.player,n.b);
                    else if (n.a==6) sd_seq_set_volume_db10(n.player,n.b);
                    break;
                case SoundOp::TrackParam:
                    if (n.b==9) sd_seq_set_pan(n.player,64+static_cast<int8_t>(n.c));
                    else if (n.b==0x0a) sd_seq_set_track_volume_db10(n.player,static_cast<unsigned>(n.a),n.c);
                    else if (n.b==0x0c) sd_seq_set_track_pitch(n.player,static_cast<unsigned>(n.a),n.c);
                    break;
                case SoundOp::Status: case SoundOp::Mode: break;
                }
            }
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
        for (unsigned player=0;player<fades.size();++player) {
            auto &fade=fades[player];
            if (!fade.rate) continue;
            fade.level+=fade.rate*(256.f/SD_MIX_RATE);
            if ((fade.rate>0 && fade.level>=fade.target) || (fade.rate<0 && fade.level<=fade.target)) {
                fade.level=fade.target; fade.rate=0;
                if (fade.stop) sd_seq_stop(static_cast<int>(player));
            }
            sd_seq_set_volume_db10(static_cast<int>(player),sd_cnv_vol(static_cast<int>(fade.level)));
        }
        sd_mix_render(block.data(), 256);
        active_.store(sd_seq_player_mask(),std::memory_order_release);
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
        const bool mono=mode_.load(std::memory_order_acquire)==1;
        for (int ch = 0; ch < 2; ch++) {
            const float va=mono?(float(pcm_[a*2])+pcm_[a*2+1])*.5f:float(pcm_[a*2+ch]);
            const float vb=mono?(float(pcm_[b*2])+pcm_[b*2+1])*.5f:float(pcm_[b*2+ch]);
            float value=va+(vb-va)*float(fraction_);
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

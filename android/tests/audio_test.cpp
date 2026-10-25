#include "audio_core.h"
#include "ntr/fs.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
static void check(bool ok) {
    if (!ok)
        throw std::runtime_error("audio assertion failed");
}
int main(int argc, char **argv) {
    try {
        uint8_t pcm8[] = {0, 0, 0x40, 0x1f, 0, 0, 0, 0, 1, 0, 0, 0, 0x80, 0, 0x7f, 0x40};
        auto c = sm64ds::decode_wave(pcm8, sizeof pcm8);
        check(c.pcm.size() == 4 && c.pcm[0] == -1 && c.pcm[3] == .5f);
        uint8_t pcm16[] = {1, 0, 0x40, 0x1f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0x80, 0xff, 0x7f};
        c = sm64ds::decode_wave(pcm16, sizeof pcm16);
        check(c.pcm.size() == 2 && c.pcm[0] == -1 && c.pcm[1] > .999f);
        uint8_t adpcm[] = {2, 0, 0x40, 0x1f, 0, 0, 1,    0,    1,    0,
                           0, 0, 0,    0,    0, 0, 0x11, 0x11, 0x11, 0x11};
        c = sm64ds::decode_wave(adpcm, sizeof adpcm);
        check(c.pcm.size() == 9 && c.pcm[1] == 1 / 32768.f && c.pcm[8] == 8 / 32768.f);
        try {
            sm64ds::decode_wave(adpcm, sizeof(adpcm) - 1);
            check(false);
        } catch (const std::runtime_error &) {
        }
        adpcm[14] = 89;
        try {
            sm64ds::decode_wave(adpcm, sizeof adpcm);
            check(false);
        } catch (const std::runtime_error &) {
        }
        sm64ds::Clip constant;
        constant.rate = 48000;
        constant.pcm.assign(4, 1.f);
        sm64ds::Mixer mixer({constant});
        float out[32];
        mixer.render(out, 16, 48000);
        for (float v : out)
            check(v == 0);
        check(mixer.trigger({0, .5f, -1, 1}));
        mixer.render(out, 16, 48000);
        check(out[0] == .5f && out[1] == 0 && out[6] == .5f && out[8] == 0);
        check(!mixer.trigger({1, .5f, 0, 1}));
        check(!mixer.trigger({0, .5f, 0, std::numeric_limits<float>::quiet_NaN()}));
        for (int i = 0; i < 64; i++)
            check(mixer.trigger({0, .5f, 0, 1}));
        check(!mixer.trigger({0, .5f, 0, 1}));
        mixer.render(out, 16, 48000);
        for (float v : out)
            check(v >= -1 && v <= 1);
        // Exercise the single-producer/consumer queue across real threads.
        std::atomic<bool> done{false};
        std::thread producer([&] {
            for (int i = 0; i < 10000; i++)
                while (!mixer.trigger({0, .2f, 0, 1}))
                    std::this_thread::yield();
            done.store(true);
        });
        while (!done.load())
            mixer.render(out, 16, 48000);
        producer.join();
        mixer.render(out, 16, 48000);
        if (argc > 1) {
            check(ntr::fs_mount_rom(argv[1]));
            auto clips = sm64ds::load_audio_clips();
            size_t total = 0;
            for (auto &clip : clips)
                total += clip.pcm.size();
            printf("ROM: %zu clips; %zu PCM samples\n", clips.size(), total);
            sm64ds::Mixer real(std::move(clips));
            check(real.trigger({0, .4f, 0, 1}));
            float power = 0;
            for (int i = 0; i < 100; i++) {
                real.render(out, 16, 48000);
                for (float v : out)
                    power += v * v;
            }
            check(power > 0);
            ntr::fs_unmount();
        }
        puts("PCM8/PCM16/ADPCM and audio mixer tests passed");
    } catch (const std::exception &e) {
        puts(e.what());
        return 1;
    }
}

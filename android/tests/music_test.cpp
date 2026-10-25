#include "pc_audio/archive.h"
#include "pc_audio/stream.h"
#include "ntr/fs.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
// This archive is generated here from a mathematical square wave and a single
// note, not from a game dump. It exercises the real PC sequencer in public CI.
static void synthetic_sequence() {
    std::vector<sd_u8> data(0x220);
    auto word = [&](size_t at, uint32_t value) {
        for (int i = 0; i < 4; i++)
            data[at + i] = value >> (i * 8);
    };
    auto half = [&](size_t at, uint16_t value) {
        data[at] = value;
        data[at + 1] = value >> 8;
    };
    auto magic = [&](size_t at, const char *value) {
        std::copy(value, value + 4, data.begin() + at);
    };
    magic(0, "SDAT");
    word(8, data.size());
    word(0x18, 0x40);
    word(0x1c, 0x80);
    word(0x20, 0xc0);
    word(0x24, 60);
    magic(0x40, "INFO");
    word(0x48, 40);
    word(0x50, 48);
    word(0x54, 56);
    word(0x68, 1);
    word(0x6c, 64);
    word(0x70, 1);
    word(0x74, 72);
    word(0x78, 1);
    word(0x7c, 84);
    word(0x80, 0);
    half(0x84, 0);
    data[0x86] = 127;
    data[0x87] = 64;
    word(0x88, 1);
    half(0x8c, 0);
    for (size_t at = 0x8e; at < 0x94; at += 2)
        half(at, 0xffff);
    word(0x94, 2);
    magic(0xc0, "FAT ");
    word(0xc8, 3);
    word(0xcc, 0x100);
    word(0xd0, 0x24);
    word(0xdc, 0x130);
    word(0xe0, 0x50);
    word(0xec, 0x180);
    word(0xf0, 0x8c);
    magic(0x100, "SSEQ");
    word(0x118, 0x1c);
    data[0x11c] = 60;
    data[0x11d] = 127;
    data[0x11e] = 48;
    data[0x11f] = 0x80;
    data[0x120] = 48;
    data[0x121] = 0xff;
    magic(0x130, "SBNK");
    word(0x168, 1);
    data[0x16c] = 1;
    half(0x16d, 0x40);
    half(0x170, 0);
    half(0x172, 0);
    data[0x174] = 60;
    data[0x175] = 127;
    data[0x176] = 127;
    data[0x177] = 127;
    data[0x178] = 127;
    data[0x179] = 64;
    magic(0x180, "SWAR");
    word(0x1b8, 1);
    word(0x1bc, 0x40);
    data[0x1c0] = 0;
    data[0x1c1] = 1;
    half(0x1c2, 8000);
    word(0x1c8, 16);
    for (size_t i = 0; i < 64; i++)
        data[0x1cc + i] = (i & 4) ? 0x40 : 0xc0;
    pc_archive_load_image(std::move(data));
    check(pc_start_sequence(0, 0), "synthetic sequence start");
    short pcm[512 * 2];
    long long energy = 0;
    for (int i = 0; i < 64; i++) {
        sd_mix_render(pcm, 512);
        for (short sample : pcm)
            energy += 1LL * sample * sample;
    }
    check(energy > 100000, "synthetic sequencer silent");
    pc_archive_close();
    printf("Synthetic SDAT sequence passed; energy %lld\n", energy);
}
int main(int argc, char **argv) {
    synthetic_sequence();
    if (argc < 2) {
        puts("PC music ROM test skipped (no ROM supplied)");
        return 0;
    }
    try {
        check(ntr::fs_mount_rom(argv[1]), "mount");
        pc_archive_load();
        check(pc_start_sequence(0, 58), "BGM58 start");
        short pcm[512 * 2];
        long long energy = 0;
        int peak = 0;
        for (int i = 0; i < 32768 * 5 / 512; i++) {
            sd_mix_render(pcm, 512);
            for (short s : pcm) {
                energy += 1LL * s * s;
                peak = std::max(peak, std::abs(int(s)));
            }
        }
        check(peak > 100 && energy > 100000, "BGM PCM is silent");
        printf("PC BGM58: peak %d, energy %lld\n", peak, energy);
        sd_seq_reset();
        sd_mix_reset();
        check(pc_start_effect(0, 0, 3), "SSAR0:3 start");
        peak = 0;
        for (int i = 0; i < 64; i++) {
            sd_mix_render(pcm, 512);
            for (short s : pcm)
                peak = std::max(peak, std::abs(int(s)));
        }
        check(peak > 100, "SSAR PCM is silent");
        printf("PC SSAR0:3: peak %d\n", peak);
        pc_archive_close();
        {
            sm64ds::MusicStream stream;
            check(stream.sequence(58), "queue BGM");
            stream.pause(false);
            float out[256 * 2]{};
            double power = 0;
            for (int i = 0; i < 100; i++) {
                std::fill(out, out + 512, 0.f);
                stream.mix(out, 256, 48000);
                for (float s : out)
                    power += s * s;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(power > 0, "PCM stream silent");
            stream.pause(true);
            stream.pause(false);
            check(stream.stop(), "queue stop");
            printf("Resampled PCM ring power %.3f; underrun callbacks %u\n", power,
                   stream.underruns());
        }
        // A second instance must start after teardown: no resident archive/worker leak.
        {
            sm64ds::MusicStream stream;
            stream.pause(false);
        }
        ntr::fs_unmount();
        puts("PC sequence/effect and streaming lifecycle tests passed");
    } catch (const std::exception &e) {
        puts(e.what());
        return 1;
    }
}

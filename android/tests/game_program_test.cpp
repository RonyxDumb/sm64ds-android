#include "runtime/game_program.h"
#include "runtime/resources.h"
#include "runtime/save_store.h"
#include "runtime/boot_seams.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace {
void put16(std::vector<uint8_t> &v, size_t at, uint16_t x) { v[at] = x; v[at + 1] = x >> 8; }
void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = x >> (i * 8); }
void narc_bounds() {
    std::vector<uint8_t> data(64);
    std::memcpy(data.data(), "NARC", 4); put16(data, 4, 0xfffe); put16(data, 6, 0x100);
    put32(data, 8, data.size()); put16(data, 12, 16); put16(data, 14, 3);
    std::memcpy(data.data() + 16, "BTAF", 4); put32(data, 20, 20); put16(data, 24, 1);
    put32(data, 28, 1); put32(data, 32, 4);
    std::memcpy(data.data() + 36, "BTNF", 4); put32(data, 40, 16);
    std::memcpy(data.data() + 52, "GMIF", 4); put32(data, 56, 12);
    data[60] = 0xaa; data[61] = 2; data[62] = 3; data[63] = 4;
    assert(sm64ds::Cartridge::narc_member(data, 0) == (std::vector<uint8_t>{2, 3, 4}));
    bool missing = false;
    try { sm64ds::Cartridge::narc_member(data, 1); } catch (const std::runtime_error &) { missing = true; }
    assert(missing);
    auto overflow = data; put32(overflow, 32, 1000); bool rejectedMember = false;
    try { sm64ds::Cartridge::narc_member(overflow, 0); } catch (const std::runtime_error &) { rejectedMember = true; }
    assert(rejectedMember);
    auto bad = data; put32(bad, 20, UINT32_MAX); bool rejected = false;
    try { sm64ds::Cartridge::narc_member(bad, 0); } catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
}
}
int main(int argc, char **argv) {
    narc_bounds();
    if (argc < 2) { std::cout << "NARC bounds passed; native boot needs a private ROM.\n"; return 0; }
    for (int repeat = 0; repeat < 2; ++repeat) {
        sm64ds::Resources resources(argv[1]);
        sm64ds::Cartridge cartridge(argv[1]);
        assert(cartridge.file(0x980e).size() >= 0x4000);
        std::string save = "/tmp/sm64ds-native-boot-" + std::to_string(getpid()) + ".sav";
        sm64ds::SaveStore store(save);
        std::vector<std::pair<uint32_t, uint32_t>> sounds;
        {
            sm64ds::GameProgram game(argv[1], [&](uint32_t a, uint32_t b) { sounds.emplace_back(a,b); return true; });
            assert(game.boot().mFadeTimer == 60 && game.boot().mState == 0);
            assert(game.requested_scene() == 0x187);
            assert(sounds.size() == 1 && sounds[0] == std::make_pair(4u, 0u));
            game.frame(1.0 / 120, 1, 0, 0, -1, -1); assert(game.ticks() == 0);
            bool reached = false, drew = false;
            try {
                for (int frame = 0; frame < 240; ++frame) {
                    game.tick();
                    if (frame == 50 && std::getenv("SM64DS_BOOT_CAPTURE")) {
                        assert(ntr::ppu_write_bmp("/tmp/sm64ds-boot-main.bmp", game.main_screen()));
                        assert(ntr::ppu_write_bmp("/tmp/sm64ds-boot-sub.bmp", game.sub_screen()));
                    }
                    if (frame > 32 && frame < 80) {
                        const auto *pixels = &game.main_screen().px[0][0];
                        for (size_t i = 0; i < 256u * 192; ++i) if (pixels[i] != 0xffffffff) drew = true;
                    }
                }
            } catch (const std::runtime_error &e) {
                assert(std::string(e.what()).find("dScDSMT_c") != std::string::npos);
                reached = true;
                std::cout << "Recovered boot reached scene " << game.requested_scene() << " after " << game.ticks() << " ticks.\n";
            }
            assert(reached && drew && game.requested_scene() == 1);
            assert(data_0209d6fc && data_0209d708 == data_0209d6fc + 0x30 && data_0209d6e8 == data_0209d6fc);
            assert(resources.heap().allocations() == 1);
        }
        assert(resources.heap().allocations() == 0 && data_0209d6fc == nullptr);
        unlink(save.c_str());
    }
    std::cout << "Native boot execution, transition and teardown passed.\n";
}

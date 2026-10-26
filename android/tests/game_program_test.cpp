#include "runtime/game_program.h"
#include "runtime/resources.h"
#include "runtime/save_store.h"
#include "runtime/boot_seams.h"
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace cstd {
s16 atan2(s32, s32);
int sqrt(unsigned long long);
s32 fdiv(s32, s32);
}
namespace {
void put16(std::vector<uint8_t> &v, size_t at, uint16_t x) {
    v[at] = x;
    v[at + 1] = x >> 8;
}
void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v[at + i] = x >> (i * 8);
}
void narc_bounds() {
    std::vector<uint8_t> data(64);
    std::memcpy(data.data(), "NARC", 4);
    put16(data, 4, 0xfffe);
    put16(data, 6, 0x100);
    put32(data, 8, data.size());
    put16(data, 12, 16);
    put16(data, 14, 3);
    std::memcpy(data.data() + 16, "BTAF", 4);
    put32(data, 20, 20);
    put16(data, 24, 1);
    put32(data, 28, 1);
    put32(data, 32, 4);
    std::memcpy(data.data() + 36, "BTNF", 4);
    put32(data, 40, 16);
    std::memcpy(data.data() + 52, "GMIF", 4);
    put32(data, 56, 12);
    data[60] = 0xaa;
    data[61] = 2;
    data[62] = 3;
    data[63] = 4;
    assert(sm64ds::Cartridge::narc_member(data, 0) == (std::vector<uint8_t>{2, 3, 4}));
    bool missing = false;
    try {
        sm64ds::Cartridge::narc_member(data, 1);
    } catch (const std::runtime_error &) {
        missing = true;
    }
    assert(missing);
    auto overflow = data;
    put32(overflow, 32, 1000);
    bool rejectedMember = false;
    try {
        sm64ds::Cartridge::narc_member(overflow, 0);
    } catch (const std::runtime_error &) {
        rejectedMember = true;
    }
    assert(rejectedMember);
    auto bad = data;
    put32(bad, 20, UINT32_MAX);
    bool rejected = false;
    try {
        sm64ds::Cartridge::narc_member(bad, 0);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);
}

void stage_controls(const char *rom) {
    sm64ds::Resources resources(rom);
    sm64ds::Cartridge cartridge(rom);
    sm64ds::StageFlow stage(cartridge);
    sm64ds::GameInput input;
    auto *active = reinterpret_cast<uint8_t *>(data_0209caa0);
    active[8] |= 0x80;
    SetNumPlayers(1);
    InitControllerMode(0, 0);
    stage.prepare_input();
    const auto &control = stage.controller(0);
    auto sample = [&](unsigned keys = 0, int x = -1, int y = -1, unsigned camera = 0) {
        input.update(keys, 0, 0, x, y);
        stage.update_input(camera);
    };
    assert(&data_0209f4ae[0] == &data_0209f498[0].mode);
    assert(&data_0209f4ae[3 * 0x18] == &data_0209f498[3].mode);
    assert(reinterpret_cast<uintptr_t>(data_0209214c[0]) > UINT32_MAX);
    sample(0x10);
    assert(control.mag == 4096 && control.nx == 4096 && control.ny == 0);
    sample(0x20);
    assert(control.mag == 4096 && control.nx == -4096 && control.ny == 0);
    sample(0x30); // the recovered low-level input removes opposing directions
    assert(control.mag == 0);
    sample(1);
    assert(data_0209214c[0][0] && control.held == data_0209214c[0][0]);
    assert(control.pressed == control.held);
    sample(1);
    assert(control.pressed == 0 && control.held == data_0209214c[0][0]);
    sample();
    assert(control.held == 0 && control.pressed == 0);

    // Original analog touch origin, five-frame delay and Q12 radius clamp.
    stage.prepare_input();
    for (int i = 0; i < 5; ++i)
        sample(0, 100, 80);
    assert(control.tx == 100 && control.ty == 80 && control.mag == 0);
    sample(0, 148, 80);
    assert(control.tx == 112 && control.mag == 4096 && control.nx == 4096 && control.ang == 0x4000);
    sample(0, 64, 80);
    assert(control.tx == 100 && control.mag == 4096 && control.nx == -4096 &&
           control.ang == -0x4000);
    input.update(0, .5f, .5f, 64, 80, false);
    stage.update_input(0, input.x(), input.y());
    assert(control.nx == -4096 && control.ny == 0); // stylus gesture keeps priority
    sample();
    assert(control.touching == 0 && control.mag == 0 && control.cnt == 4);
    sample(0, 80, 80);
    assert(control.touching == 1 && control.nx == -4096);
    sample();

    // Camera hit areas are the source's regions, guarded by actual camera flags.
    sample(0, 20, 170, 0x1000);
    assert(control.held == 0x200 && control.pressed == 0x200 && data_0209f350[0] == 1);
    sample(0, 50, 170, 0x1000);
    assert(control.held == 0x100 && control.pressed == 0x100);
    sample();
    assert(control.held == 0 && data_0209f350[0] == 0);
    InitControllerMode(0, 1);
    sample(0, 220, 150);
    assert((control.pressed & 0x8000) && data_ov002_02111180 == 0x10);
    sample();

    for (unsigned mode = 1; mode < 3; ++mode) {
        InitControllerMode(0, mode);
        sample(0x10);
        assert(control.mag == 0); // the other modes use stylus movement
        sample(1);
        assert(control.held == data_0209214c[mode][0]);
        sample();
    }
    input.update(0, .25f, .5f, -1, -1, false);
    stage.update_input(0, input.x(), input.y());
    assert(control.nx == 1024 && control.ny == 2048 && control.mag == 2290);
    assert(control.ang == cstd::atan2(1024, 2048));
    assert(input.held() == 0 && control.held == 0); // no synthetic camera/d-pad buttons
    input.update(0, 1, 1, -1, -1, false);
    stage.update_input(0, input.x(), input.y());
    assert(control.nx == 2896 && control.ny == 2896 && control.mag == 4096);
    input.update(0, 0, 0, -1, -1);
    stage.update_input(0);
    assert(control.mag == 0 && control.nx == 0 && control.ny == 0);
    active[8] &= ~0x80;
    sample(1);
    assert(control.held == 0 && control.pressed == 0 && control.mag == 0);
    stage.update_input(0, .5f, .5f);
    assert(control.mag == 0); // physical axes must not bypass the game's input gate
    InitControllerMode(0, 3);
    bool rejected = false;
    try {
        stage.update_input(0);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);

    // Exact ROM lookup/quadrant behavior, including all four diagonal signs.
    assert(cstd::atan2(1, 1) == 0x2000);
    assert(cstd::atan2(1, -1) == 0x6000);
    assert(cstd::atan2(-1, -1) == -0x6000);
    assert(cstd::atan2(-1, 1) == -0x2000);
    assert(cstd::sqrt(0) == 0 && cstd::sqrt(2) == 1 && cstd::sqrt(3) == 2);
    assert(cstd::sqrt(2304) == 48 && cstd::fdiv(1, 6) == 683);
    assert(cstd::fdiv(-1, 6) == -683);
    std::cout
        << "Recovered stage controls: button maps, touch, camera regions and integer math passed.\n";
}
}
int main(int argc, char **argv) {
    narc_bounds();
    if (argc < 2) {
        std::cout << "NARC bounds passed; native boot needs a private ROM.\n";
        return 0;
    }
    for (int repeat = 0; repeat < 4; ++repeat) {
        sm64ds::Resources resources(argv[1]);
        sm64ds::Cartridge cartridge(argv[1]);
        assert(cartridge.file(0x980e).size() >= 0x4000);
        std::string save = "/tmp/sm64ds-native-boot-" + std::to_string(getpid()) + ".sav";
        sm64ds::SaveStore store(save);
        // Distinct real cartridge slots make a reversed CpuCopy8 or wrong
        // title-result mapping visible; the fourth session keeps blank defaults.
        std::array<sm64ds::SaveStore::FileData, 3> expected{};
        for (unsigned slot = 0; slot < expected.size(); ++slot) {
            assert(store.load_file(slot, expected[slot]));
            if (repeat < 3) {
                expected[slot][0x41] = slot;
                expected[slot][0x42] = slot;
                expected[slot][8] |= 1u << slot;
                expected[slot][0x14] = 0x10 + slot;
                expected[slot][0x23] = 0x21;
                expected[slot][0x32] = 50 + slot;
                assert(store.save_file(slot, expected[slot]));
            }
        }
        std::vector<std::pair<uint32_t, uint32_t>> sounds;
        {
            sm64ds::GameProgram game(argv[1], [&](uint32_t a, uint32_t b) {
                sounds.emplace_back(a, b);
                return true;
            });
            assert(game.boot().mFadeTimer == 60 && game.boot().mState == 0);
            assert(game.requested_scene() == 0x187);
            bool secondRejected = false;
            try {
                sm64ds::GameProgram second(argv[1], [](uint32_t, uint32_t) { return true; });
            } catch (const std::runtime_error &) {
                secondRejected = true;
            }
            assert(secondRejected && game.requested_scene() == 0x187);
            assert(sounds.size() == 1 && sounds[0] == std::make_pair(4u, 0u));
            game.frame(1.0 / 120, 1, 0, 0, -1, -1);
            assert(game.ticks() == 0);
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
                        for (size_t i = 0; i < 256u * 192; ++i)
                            if (pixels[i] != 0xffffffff)
                                drew = true;
                    }
                }
            } catch (const std::runtime_error &e) {
                assert(std::string(e.what()).find("dScDSMT_c") != std::string::npos);
                reached = true;
                std::cout << "Recovered boot reached scene " << game.requested_scene() << " after "
                          << game.ticks() << " ticks.\n";
            }
            assert(reached && drew && game.requested_scene() == 1);
            assert(game.ticks() == 120); // 60-frame boot hold and two 30-frame source fades
            assert(game.title_files() && data_0209b33c);
            assert(reinterpret_cast<uintptr_t>(data_0209b33c) > UINT32_MAX);
            for (unsigned slot = 0; slot < expected.size(); ++slot) {
                assert(game.title_files()->read_ok(slot));
                assert(std::memcmp(&game.title_files()->file(slot), expected[slot].data(),
                                   expected[slot].size()) == 0);
                const auto summary = game.title_files()->summary(slot);
                assert(summary.used == (repeat < 3));
                assert(summary.characters == (repeat < 3 ? slot + 1 : 0));
                const uint8_t starBits = expected[slot][0x14];
                assert(summary.starZero[0] == (starBits & 1));
                assert(summary.stars[0] == __builtin_popcount(starBits) - (starBits & 1));
                assert(summary.coins[0] == expected[slot][0x32]);
                assert(summary.secretStars == (repeat < 3 ? 2 : 0));
            }
            assert(game.title_files()->config().messageBank == data_0209d6fc);
            assert(game.title_files()->config().bytes[8] == 0);
            assert(data_0209d6fc && data_0209d708 == data_0209d6fc + 0x30 &&
                   data_0209d6e8 == data_0209d6fc);
            assert(resources.heap().allocations() == 1);
            auto *active = reinterpret_cast<uint8_t *>(data_0209caa0);
            std::vector<uint8_t> minigames(active + 0x44, active + 0x328);
            assert(!game.accept_title_result(2) && game.requested_scene() == 1);
            assert(!game.accept_title_result(INT_MIN) && game.requested_scene() == 1);
            const unsigned selected = repeat < 3 ? repeat : 0;
            assert(game.accept_title_result(3 + selected));
            assert(std::memcmp(active, expected[selected].data(), 0x44) == 0);
            assert(active[0x328] == selected);
            assert(std::memcmp(active + 0x44, minigames.data(), minigames.size()) == 0);
            auto request = game.stage_request();
            assert(request.level == 1 && request.entrance == 0 && request.star == 1);
            assert(request.players == 1 && request.character == expected[selected][0x41]);
            assert(request.controllerMode == expected[selected][0x42]);
            assert(game.requested_scene() == 3 && data_0209f5e8.color == 0x7fff);
            for (unsigned slot = 0; slot < 4; ++slot) {
                assert(data_0209f4ae[slot * 0x18] == expected[selected][0x42]);
                assert(data_02092144[slot] == 0x880);
                assert(data_0209fc5c[slot] == (slot == 0));
                assert(data_0209fc64[slot] == (slot == 0 ? 0 : -1));
            }
            bool reachedStage = false;
            try {
                game.tick();
            } catch (const std::runtime_error &e) {
                reachedStage = std::string(e.what()).find("dScStage_c") != std::string::npos;
            }
            assert(reachedStage);
            // This source path previously wrote the DS +0xc offset into the
            // native fader's speed. Verify the actual native color member.
            data_02092110 = -1;
            data_0209f5e8.color = 0x1234;
            LoadLevel(1, 0, 1, 0, -1);
            assert(data_0209f5e8.color == 0);
            std::cout << "Recovered title result " << 3 + selected << " selected character "
                      << request.character << ", mode " << request.controllerMode
                      << " and requested castle grounds / scene 3.\n";
        }
        assert(resources.heap().allocations() == 0 && data_0209d6fc == nullptr &&
               data_0209b33c == nullptr);
        unlink(save.c_str());
    }
    stage_controls(argv[1]);
    std::cout << "Native boot, original file launch, stage request and teardown passed.\n";
}

#include "runtime/game_program.h"
#include "runtime/resources.h"
#include "runtime/save_store.h"
#include "pc_audio/stream.h"
#include "runtime/boot_seams.h"
#include "runtime/packed_ptr.h"
#include "ntr/lz77.h"
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
extern "C" void MultiCopy_Int(int *, int *, int);
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

void packed_card_io(sm64ds::packed::Space &space) {
    using sm64ds::packed::Ptr;
    sm64ds::packed::Space::Scope scope(space);
    const auto file=space.allocate(0x44),path=space.allocate(20),target=space.allocate(64);
    std::memcpy(space.data(path,19),"/ARCHIVE/arc0.narc",19);
    Ptr<void(uint32_t)>(0x0205d874u)(file); // original FS_InitFile
    assert(Ptr<int(uint32_t,uint32_t)>(0x0205d518u)(file,path)==1);
    const uint32_t top=*Ptr<uint32_t>(file+0x20),bottom=*Ptr<uint32_t>(file+0x24);
    assert(bottom>top && *Ptr<uint32_t>(file+0x28)==top);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,target,16,0)==16);
    assert(std::memcmp(space.data(target,4),"NARC",4)==0);
    assert(*Ptr<uint32_t>(file+0x28)==top+16);
    *Ptr<uint32_t>(file+0x28)=bottom-4;
    std::memset(space.data(target,64),0xa5,64);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,target,64,1)==4);
    assert(*Ptr<uint32_t>(file+0x28)==bottom);
    assert((*Ptr<uint32_t>(file+0xc)&0xf)==0 && *Ptr<uint32_t>(file+0x14)==0);
    for(unsigned i=4;i<64;++i) assert(*Ptr<uint8_t>(target+i)==0xa5);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,0,16,0)==0);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,0,-1,0)==0);
    assert(Ptr<int(uint32_t)>(0x0205d4ccu)(file)==1);
    assert(*Ptr<uint32_t>(file+8)==0 && *Ptr<uint32_t>(file+0x10)==0xc);
    assert(Ptr<int(uint32_t)>(0x0205d4ccu)(file)==0);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,target,4,0)==-1);
    // FS_LoadArchiveTables changes FAT/FNT lanes to absolute handles and
    // sets TABLE_LOAD. Exercise the same public open/read/close operations.
    const uint32_t archive=*Ptr<uint32_t>(0x020a804cu);
    assert(archive==0x020a8074u); // SDK's inline ROM archive, used by overlays
    const uint32_t base=*Ptr<uint32_t>(archive+0x1c),flags=*Ptr<uint32_t>(archive+0x10);
    const uint32_t fat=*Ptr<uint32_t>(archive+0x20),fatSize=*Ptr<uint32_t>(archive+0x24);
    const uint32_t fnt=*Ptr<uint32_t>(archive+0x28),fntSize=*Ptr<uint32_t>(archive+0x2c);
    const uint32_t tables=space.allocate(fatSize+fntSize,32);
    std::memcpy(space.data(tables,fatSize),space.data(base+fat,fatSize),fatSize);
    std::memcpy(space.data(tables+fatSize,fntSize),space.data(base+fnt,fntSize),fntSize);
    *Ptr<uint32_t>(archive+0x20)=tables;
    *Ptr<uint32_t>(archive+0x28)=tables+fatSize;
    *Ptr<uint32_t>(archive+0x10)=flags|4;
    Ptr<void(uint32_t)>(0x0205d874u)(file);
    assert(Ptr<int(uint32_t,uint32_t)>(0x0205d518u)(file,path)==1);
    assert(Ptr<int(uint32_t,uint32_t,int,int)>(0x0205d688u)(file,target,16,0)==16);
    assert(std::memcmp(space.data(target,4),"NARC",4)==0);
    assert(Ptr<int(uint32_t)>(0x0205d4ccu)(file)==1);
    *Ptr<uint32_t>(archive+0x20)=fat;
    *Ptr<uint32_t>(archive+0x28)=fnt;
    *Ptr<uint32_t>(archive+0x10)=flags;
    space.release(tables);
    space.release(target);space.release(path);space.release(file);
}

void startup_data(const char *rom) {
    sm64ds::Cartridge cartridge(rom);
    const auto runtime=cartridge.arm9_data(0x020aa420u);
    const auto params=cartridge.arm9(0x02004ad8u,36);
    auto word=[](const uint8_t *p) {
        return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
    };
    const uint32_t bss=word(params.data()+12);
    for(size_t i=bss-runtime.base;i<runtime.bytes.size();++i) assert(runtime.bytes[i]==0);
    const auto table=cartridge.arm9(0x0208ecf4u,13*20);
    assert(std::memcmp(runtime.bytes.data()+0x0208ecf4u-runtime.base,table.data(),table.size())==0);
    assert(runtime.relocated.size()==2);
    const auto &itcm=runtime.relocated[0],&dtcm=runtime.relocated[1];
    assert(itcm.base==0x01ff8000u && itcm.bytes.size()==0x5f40);
    const auto expected=cartridge.arm9(word(params.data()+8),0x5f40);
    assert(itcm.bytes==expected);
    assert(dtcm.base==0x023c0000u && dtcm.bytes.size()==0x4000);
    for(unsigned i=0;i<0x3ffc;++i) assert(dtcm.bytes[i]==0);
    assert(word(dtcm.bytes.data()+0x3ffc)==0x01ffd97cu);
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
        if (repeat==0) startup_data(argv[1]);
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
        sm64ds::MusicStream music; music.pause(false);
        std::vector<std::pair<uint32_t, uint32_t>> sounds;
        {
            sm64ds::GameProgram game(argv[1], [&](uint32_t a, uint32_t b) {
                sounds.emplace_back(a, b);
                return music.effect(static_cast<int>(a),static_cast<int>(b));
            },[&](const sm64ds::SoundRequest &request){ return music.native(request); });
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
            bool drew = false;
            for (int frame=0;frame<160;++frame) {
                game.tick();
                float pcm[1024]{}; music.mix(pcm,512,32768);
                if (frame>32 && frame<80) {
                    const auto *pixels=&game.main_screen().px[0][0];
                    for (size_t i=0;i<256u*192;++i) if (pixels[i]!=0xffffffff) drew=true;
                }
            }
            assert(drew && game.active_scene()==1 && game.requested_scene()==0x187);
            assert(game.ticks()==160);
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
            assert(game.frontend_core() && game.frontend_core()->has_items());
            assert(game.frontend_core()->allocations() > 179);
            const auto &front = *game.frontend_core();
            const auto root = front.root_address();
            auto display = front.word_at(root + 0x74);
            assert(front.word_at(display + 0x20) == 4);
            assert(front.word_at(display + 0x28) == 6);
            assert(front.word_at(display + 0x30) == 8);
            assert(front.word_at(display + 0x38) == 6);
            for (unsigned i = 0; i < 4; ++i) {
                auto character = front.word_at(front.word_at(display + 0x24) + 4 * i);
                assert(character && front.word_at(character));
            }
            for (unsigned i = 0; i < 8; ++i) {
                auto slot = front.word_at(root + 0xc8 + 4 * i);
                uint8_t kind;
                std::memcpy(&kind, front.data_at(slot + 8, 1), 1);
                assert(kind == i && front.word_at(slot) && front.word_at(slot + 4));
            }
            assert(front.word_at(root + 0x184) == 0);
            std::cout << "Recovered frontend root: " << front.allocations()
                      << " allocations, 24 item views and 8 BG slots.\n";
            for (unsigned i = 0; i < 24; ++i) {
                auto view = front.word_at(root + 0x114 + i * 4);
                auto item = front.word_at(view);
                uint16_t id;
                std::memcpy(&id, front.data_at(item, 2), 2);
                assert(id == i);
                auto sprite = front.word_at(view + 4);
                auto frames = front.word_at(sprite + 0x5c);
                bool originalTexture = false;
                for (unsigned j = 0; j < 11; ++j)
                    originalTexture |= front.word_at(frames) == front.word_at(root + 0x78 + j * 4);
                assert(originalTexture);
                assert(front.word_at(front.word_at(view + 0x10) + 0x10) == view + 0xc);
            }
            assert(resources.heap().allocations() == 1 + game.frontend_core()->allocations());
            auto *active = reinterpret_cast<uint8_t *>(data_0209caa0);
            std::vector<uint8_t> minigames(active + 0x44, active + 0x328);
            assert(!game.accept_title_result(2) && game.requested_scene() == 0x187);
            assert(!game.accept_title_result(INT_MIN) && game.requested_scene() == 0x187);
            const unsigned selected = repeat < 3 ? repeat : 0;
            assert(game.accept_title_result(3 + selected));
            assert(std::memcmp(active, expected[selected].data(), 0x44) == 0);
            assert(active[0x328] == selected);
            assert(std::memcmp(active + 0x44, minigames.data(), minigames.size()) == 0);
            auto request = game.stage_request();
            assert(request.level == 1 && request.entrance == 0 && request.star == 1);
            const auto *assets = game.stage_assets();
            assert(assets && assets->descriptor().level == 1);
            assert(assets->descriptor().overlayId == 9);
            assert(assets->descriptor().levelAddress == 0x02112bdc);
            assert(!assets->model().empty() && !assets->collision().empty());
            assert(assets->overlay_at(assets->descriptor().levelAddress, 0x1c));
            // Every stage uses the recovered native fBase_c defaults.
            // Subclass virtuals deliberately remain unbound until ported.
            assert(game.stage_address_space());
            assert(game.stage_address_space()->code(0x02043afcu));
            assert(game.stage_address_space()->code(0x02043ac8u));
            // Every projected actor record is still the original overlay
            // payload; no native pointer or fabricated spawn has entered it.
            assert(!assets->objects().empty());
            // Pinned European cartridge / castle grounds (sublevel 1):
            // category callback addresses and the real entrance record.
            assert(assets->entrances().size() == 14);
            assert(assets->standard_spawns().size() == 49);
            assert(assets->simple_spawns().size() == 40);
            const auto &firstEntrance = assets->entrances()[0];
            assert(firstEntrance.actorIndex == 0 && firstEntrance.actorId == 0xbf);
            assert(firstEntrance.x == -1200 * 4096 &&
                   firstEntrance.y == 254 * 4096 &&
                   firstEntrance.z == 6800 * 4096);
            assert(firstEntrance.entrance && firstEntrance.param == 0);

            for (const auto &object : assets->objects()) {
                assert(object.type < 32 && object.starFilter < 8);
                assert(std::memcmp(object.data.data(),
                                   assets->overlay_at(object.guestAddress, 8), 8) == 0);
                assert(object.area >= -1 &&
                       object.area < assets->descriptor().subTableCount);
            }
            auto *stageSpace = game.stage_address_space();
            assert(stageSpace);
            packed_card_io(*stageSpace);
            assert(stageSpace->data(assets->descriptor().levelAddress, 0x1c) ==
                   assets->overlay_at(assets->descriptor().levelAddress, 0x1c));
            assert(stageSpace->address(assets->overlay_at(
                assets->descriptor().levelAddress, 0x1c)) ==
                   assets->descriptor().levelAddress);
            // The recovered actor dispatch address and ID table are read
            // from the cartridge's actual overlay 2, never generated IDs.
            const auto &actorOv = assets->object_overlay();
            assert(stageSpace->data(actorOv.base, actorOv.bytes.size()) ==
                   actorOv.bytes.data());
            for (const auto &category : assets->objects()) {
                assert(category.entryCount == category.data[1]);
                assert(category.loaderAddress ==
                       sm64ds::StageAssets::original_loader(actorOv, category.type));
            }
            for (const auto &actor : assets->simple_spawns()) {
                assert(actor.actorIndex <= 0x1ffu);
                if (actor.minimapChange) {
                    assert(actor.actorIndex == 0x1ffu && actor.param <= 15);
                } else {
                    const auto actorOffset = 0x0210cbf4u + actor.actorIndex * 2;
                    const uint8_t *id = static_cast<const uint8_t *>(
                        stageSpace->data(actorOffset, 2));
                    assert(actor.actorId == (uint16_t(id[0]) | uint16_t(id[1] << 8)));
                }
            }
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
            uint32_t profile = 0, factory = 0;
            std::memcpy(&profile, stageSpace->data(0x02090864u + 3 * 4, 4), 4);
            std::memcpy(&factory, stageSpace->data(profile, 4), 4);
            assert(profile == 0x0209213cu && factory == 0x0202e088u);
            try {
                game.tick();
            } catch (const std::runtime_error &e) {
                reachedStage = std::string(e.what()) ==
                    "Unlinked native code handle: " + std::to_string(0x0202ded4u);
                if (std::getenv("SM64DS_TRACE_STAGE")) {
                    std::cerr << "Actual stage dependency: " << e.what() << '\n';
                    reachedStage=std::string(e.what()).find("Unlinked native code handle: ")==0;
                }
                if (!reachedStage) std::cerr << "Stage initialization: " << e.what() << '\n';
            }
            assert(reachedStage);
            assert(*static_cast<uint8_t *>(stageSpace->data(0x020a4b4cu, 1)) == 5);
            auto stageWord = [&](uint32_t address) {
                uint32_t value=0;
                std::memcpy(&value,stageSpace->data(address,4),4);
                return value;
            };
            const uint32_t actor=stageWord(0x020a4b6cu)-0x14;
            assert(stageSpace->allocation_size(actor)==0x9c8);
            assert(stageWord(actor)==0x020921c0u);
            assert(*static_cast<uint16_t *>(stageSpace->data(actor+12,2))==3);
            assert(*static_cast<uint8_t *>(stageSpace->data(actor+0x13,1))==5);
            for (unsigned offset : {0x24u,0x30u,0x40u}) assert(stageWord(actor+offset)==actor);
            assert(stageWord(0x0209ee74u)==actor+0x50);
            assert(stageWord(actor+0x50+0x804)==0x4b000);
            assert(stageWord(actor+0x50+0x7f0)==0x0208f3f4u);
            assert(stageWord(actor+0x50+0x810)==0x0208f434u);
            assert(*static_cast<uint8_t *>(stageSpace->data(actor+0x50+0x814,1))==1);
            assert(stageWord(actor+0x86c)==0x0208e90cu);
            assert(std::memcmp(stageSpace->data(actor+0x86c+0x1c,48),
                               stageSpace->data(0x02082128u,48),48)==0);
            assert(stageWord(actor+0x91c)==0x020993dcu);
            assert(stageWord(actor+0x91c+8)==UINT32_MAX);
            assert(stageWord(actor+0x91c+12)==uint32_t(-0x1000));
            assert(*static_cast<uint8_t *>(stageSpace->data(actor+0x91c+20,1))==0x18);
            assert(stageWord(actor+0x9c4)==0);
            assert(stageWord(0x0209f5c0u)==actor);
            assert(stageWord(0x0209f314u)==actor+0x8bc);
            assert(*static_cast<uint8_t *>(stageSpace->data(0x0209f2f8u,1))==1);
            assert(*static_cast<uint8_t *>(stageSpace->data(0x0209f21cu,1))==1);
            // Original LoadArchive/func_02018934 and NARC mounting ran. These
            // point at genuine owned archive bytes, never residency sentinels.
            const unsigned languageArchive=GetOwnerLanguage()==2?9:GetOwnerLanguage()==3?10:
                                           GetOwnerLanguage()==4?11:GetOwnerLanguage()==5?12:8;
            for (unsigned archive : {0u,6u,7u,languageArchive}) {
                const uint32_t record=stageWord(0x0208ecf4u+archive*20);
                assert(record && (record&15)==0);
                assert(stageSpace->allocation_size(record)>0x60);
                assert(stageWord(record+0x50)==record+0x60);
                assert(std::memcmp(stageSpace->data(record+0x60,4),"NARC",4)==0);
                assert((stageWord(record+0x10)&2)!=0);
                assert(stageWord(record+0x54)>=record+0x60);
                assert(stageWord(record+0x58)>stageWord(record+0x54));
            }
            for (unsigned character=0;character<3;++character) {
                const bool unlocked=(uint32_t(expected[selected][8])&(1u<<character))!=0;
                assert(*static_cast<uint8_t *>(stageSpace->data(0x020ad628u+character,1))==
                       (unlocked?0x10:0));
                assert(stageWord(0x020ad634u+character*4)==0);
            }
            assert(game.requested_scene() == 3); // failed spawn never publishes success
            // This source path previously wrote the DS +0xc offset into the
            // native fader's speed. Verify the actual native color member.
            data_02092110 = -1;
            data_0209f5e8.color = 0x1234;
            LoadLevel(1, 0, 1, 0, -1);
            assert(data_0209f5e8.color == 0);
            // Native boot helpers must accept both original logical DS video
            // addresses and relocated host pointers, without writing outside
            // VRAM or palette bounds. These assets come from the private ROM.
            const int decodedSize = ntr::lz77_size(data_020918c4, sizeof data_020918c4);
            assert(decodedSize > 0 && decodedSize <= 0x20000);
            std::vector<uint8_t> reference(static_cast<size_t>(decodedSize));
            assert(ntr::lz77_unpack(data_020918c4, sizeof data_020918c4,
                                    reference.data(), decodedSize) == decodedSize);
            constexpr uint32_t vramTarget = ntr::VRAM_BASE + 0x800000u;
            void *nativeTarget = ntr::host_ptr(vramTarget);
            std::memset(nativeTarget, 0, static_cast<size_t>(decodedSize));
            DecompressLZ16(data_020918c4,
                           reinterpret_cast<void *>(static_cast<uintptr_t>(vramTarget)));
            assert(std::memcmp(nativeTarget, reference.data(), reference.size()) == 0);
            std::memset(nativeTarget, 0, static_cast<size_t>(decodedSize));
            DecompressLZ16(data_020918c4, nativeTarget);
            assert(std::memcmp(nativeTarget, reference.data(), reference.size()) == 0);
            bool invalidDestination = false;
            try {
                DecompressLZ16(data_020918c4, reinterpret_cast<void *>(
                    static_cast<uintptr_t>(ntr::VRAM_BASE + ntr::VRAM_SIZE - 4)));
            } catch (const std::runtime_error &) {
                invalidDestination = true;
            }
            assert(invalidDestination);
            constexpr uint32_t plttTarget = ntr::PLTT_BASE + 0x780u;
            MultiCopy_Int(reinterpret_cast<int *>(data_020914e0),
                          reinterpret_cast<int *>(static_cast<uintptr_t>(plttTarget)), 16);
            assert(std::memcmp(ntr::host_ptr(plttTarget), data_020914e0, 16) == 0);
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

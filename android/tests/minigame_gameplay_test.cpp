#include "runtime/game_program.h"
#include "runtime/resources.h"
#include "runtime/save_store.h"
#include "runtime/minigame_progress.h"
#include "pc_audio/stream.h"
#include "ntr/ppu.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

// Private-cartridge integration. Every state and score comes from original
// gameplay; this test supplies touch events and observes the scene records.
void minigame_gameplay_cartridge(const char *rom) {
    sm64ds::Resources resources(rom);
    const auto save="/tmp/sm64ds-wanted-"+std::to_string(getpid())+".sav";
    unlink(save.c_str());
    sm64ds::SaveStore store(save);
    sm64ds::MusicStream music;
    music.pause(false);
    sm64ds::GameProgram game(rom,[&](uint32_t archive,uint32_t entry) {
        return music.effect(static_cast<int>(archive),static_cast<int>(entry));
    },[&](const sm64ds::SoundRequest &r) { return music.native(r); });
    unsigned frame=0;
    const auto tick=[&](int x=-1,int y=-1,uint32_t keys=0) {
        try { game.tick(keys,0,0,x,y); }
        catch (const std::exception &e) {
            std::cerr<<"Wanted frame "<<frame<<", active="<<game.active_scene()<<": "<<e.what()<<'\n';
            throw;
        }
        ++frame;
        float pcm[1096]{}; music.mix(pcm,548,32768);
    };
    for (unsigned i=0;i<160;++i) tick();
    tick(128,96); tick();
    for (unsigned i=0;i<488;++i) tick();
    tick(220,170); tick();
    for (unsigned i=0;i<200 && game.active_scene()!=5;++i) tick();
    for (unsigned i=0;i<40;++i) tick();
    assert(game.active_scene()==5 && game.minigame_runtime());
    size_t row=0;
    while (row<36 && game.minigames().row(row).sceneId!=0x16e) ++row;
    assert(row<36);
    std::cout<<"Wanted original catalog row "<<row<<std::endl;
    sm64ds::MinigameProgress::unlock(row);
    const int group=static_cast<int>(row%4);
    const int tab=group==3?0:group+1;
    tick(0x18,0x18+tab*0x30); tick();
    for (unsigned i=0;i<40;++i) tick();
    const size_t cell=row/4;
    if (cell>=6) {
        tick(0xe0,0x60); tick();
        for (unsigned i=0;i<60;++i) tick();
    }
    const int x=cell<6?0x58+static_cast<int>(cell%3)*0x30:0x78+static_cast<int>(cell-6)*0x30;
    const int y=cell<6?0x38+static_cast<int>(cell/3)*0x48:0x58;
    tick(x,y); tick();
    assert(game.requested_scene()==0x16e);
    for (unsigned i=0;i<300;++i) tick();
    const auto *runtime=game.minigame_gameplay_runtime();
    assert(runtime && game.active_scene()==0x16e && runtime->actor());
    const auto &space=runtime->space();
    const auto actor=runtime->actor();
    const auto word=[&](uint32_t a) { int32_t v; std::memcpy(&v,space.data(a,4),4); return v; };
    const auto byte=[&](uint32_t a) { return *static_cast<const uint8_t *>(space.data(a,1)); };
    assert(byte(actor+14)==1);
    assert(word(actor+0x4f78)==2); // original STATE_PLAY
    assert(byte(actor+0x5455)); // original board placement completed
    unsigned live=0;
    for (unsigned i=0;i<120;++i) live+=byte(actor+0x52ed+i)!=0;
    assert(live>1);
    size_t spritePixels=0;
    for (const auto &screen:{&game.main_screen(),&game.sub_screen()})
        for (const auto &line:screen->layer) for (auto layer:line) spritePixels+=layer==4;
    assert(spritePixels>100);
    if (const auto *capture=std::getenv("SM64DS_WANTED_CAPTURE")) {
        assert(ntr::ppu_write_bmp((std::string(capture)+"-main.bmp").c_str(),game.main_screen()));
        assert(ntr::ppu_write_bmp((std::string(capture)+"-sub.bmp").c_str(),game.sub_screen()));
    }
    std::cout<<"Wanted launched: original board, "<<live<<" pictures, touch/timer loop and "<<spritePixels<<" OBJ pixels."<<std::endl;
    unlink(save.c_str());
}

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
    const bool amida=std::getenv("SM64DS_TEST_AMIDA")!=nullptr;
    const uint32_t scene=amida?0x173u:0x16eu;
    const char *name=amida?"Amida":"Wanted";
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
            std::cerr<<name<<" frame "<<frame<<", active="<<game.active_scene()<<": "<<e.what()<<'\n';
            throw;
        }
        ++frame;
        float pcm[1096]{}; music.mix(pcm,548,32768);
    };
    const auto idle=[&](unsigned frames) {
        // Exercise Android's real four-tick catch-up path. It still scans
        // capture frames immediately; ordinary idle presentation is batched.
        while (frames>=4) {
            try { game.frame(4.0/59.8261+1e-10,0,0,0,-1,-1); }
            catch (const std::exception &e) {
                std::cerr<<name<<" idle frame "<<frame<<", active="<<game.active_scene()<<": "<<e.what()<<'\n';
                throw;
            }
            frame+=4; frames-=4;
            float pcm[4384]{}; music.mix(pcm,2192,32768);
        }
        while (frames--) tick();
    };
    idle(160);
    tick(128,96); tick();
    idle(488);
    tick(220,170); tick();
    for (unsigned i=0;i<200 && game.active_scene()!=5;++i) tick();
    idle(40);
    assert(game.active_scene()==5 && game.minigame_runtime());
    size_t row=0;
    while (row<36 && game.minigames().row(row).sceneId!=scene) ++row;
    assert(row<36);
    std::cout<<name<<" original catalog row "<<row<<std::endl;
    sm64ds::MinigameProgress::unlock(row);
    const int group=static_cast<int>(row%4);
    const int tab=group==3?0:group+1;
    tick(0x18,0x18+tab*0x30); tick();
    idle(40);
    const size_t cell=row/4;
    if (cell>=6) {
        tick(0xe0,0x60); tick();
        idle(60);
    }
    const int x=cell<6?0x58+static_cast<int>(cell%3)*0x30:0x78+static_cast<int>(cell-6)*0x30;
    const int y=cell<6?0x38+static_cast<int>(cell/3)*0x48:0x58;
    tick(x,y); tick();
    assert(game.requested_scene()==scene);
    idle(300);
    const auto *runtime=game.minigame_gameplay_runtime();
    assert(runtime && game.active_scene()==static_cast<int>(scene) && runtime->actor());
    const auto &space=runtime->space();
    const auto actor=runtime->actor();
    const auto word=[&](uint32_t a) { int32_t v; std::memcpy(&v,space.data(a,4),4); return v; };
    const auto byte=[&](uint32_t a) { return *static_cast<const uint8_t *>(space.data(a,1)); };
    assert(byte(actor+14)==1);
    unsigned live=0;
    if (amida) {
        assert(word(actor+0x46d0)==1); // original walker/pen gameplay
        const uint32_t stroke=static_cast<uint32_t>(word(actor+0x470c));
        const uint32_t ink=static_cast<uint32_t>(word(actor+0x4710));
        assert(stroke && ink && stroke!=ink);
        assert(space.allocation_size(stroke)>=0x15800 && space.allocation_size(ink)>=0x15800);
        live=static_cast<unsigned>(word(actor+0x46c8));
        assert(live>0 && live<=4);
    } else {
        assert(word(actor+0x4f78)==2); // original STATE_PLAY
        assert(byte(actor+0x5455)); // original board placement completed
        for (unsigned i=0;i<120;++i) live+=byte(actor+0x52ed+i)!=0;
        assert(live>1);
    }
    size_t spritePixels=0;
    size_t backgroundPixels=0;
    for (const auto &screen:{&game.main_screen(),&game.sub_screen()})
        for (unsigned py=0;py<ntr::SCREEN_H;++py) for (unsigned px=0;px<ntr::SCREEN_W;++px) {
            spritePixels+=screen->layer[py][px]==4;
            backgroundPixels+=screen->layer[py][px]<4 && (screen->px[py][px]&0xffffffu)!=0;
        }
    assert(spritePixels>100);
    if (const auto *capture=std::getenv("SM64DS_WANTED_CAPTURE")) {
        assert(ntr::ppu_write_bmp((std::string(capture)+"-main.bmp").c_str(),game.main_screen()));
        assert(ntr::ppu_write_bmp((std::string(capture)+"-sub.bmp").c_str(),game.sub_screen()));
    }
    std::cout<<name<<" launched: original board, "<<live<<" actors, "<<spritePixels
             <<" OBJ pixels and "<<backgroundPixels<<" coloured BG pixels."<<std::endl;
    assert(backgroundPixels>1000);
    if (amida) {
        tick(48,100);
        assert(word(actor+0x46f0)==48 && word(actor+0x46f4)==100);
        for (int px=52;px<=96;px+=4) tick(px,100);
        tick();
        assert(word(actor+0x46e8)==-1 && word(actor+0x46ec)==-1);
        std::cout<<"Amida original pen tracking, stroke and release executed."<<std::endl;
    } else {
        const unsigned target=byte(actor+0x5456);
        assert(target>=1 && target<=120);
        const auto level=word(actor+0xbc);
        tick(word(actor+0x47f8+(target-1)*4)>>12,word(actor+0x49d8+(target-1)*4)>>12);
        tick();
        assert(byte(actor+0x5459)==1 && word(actor+0x4f78)==3);
        for (unsigned i=0;i<600 && (word(actor+0xbc)==level || word(actor+0x4f78)!=2);++i) tick();
        assert(word(actor+0xbc)==level+1 && word(actor+0x4f78)==2);
        assert(word(actor+0xb4)>0 && byte(actor+0x5457)>0);
        std::cout<<"Wanted original correct touch, reward, earned score "<<word(actor+0xb4)
                 <<" and next board passed."<<std::endl;
    }
    unlink(save.c_str());
}

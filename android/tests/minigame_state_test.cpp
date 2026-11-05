#include "runtime/minigame_shared.h"
#include "runtime/cartridge.h"
#include "runtime/packed_ptr.h"
#include "runtime/boot_seams.h"
#include "runtime/minigame_progress.h"
#include "runtime/save_store.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace sm64ds::minigame_sources { void bind_sources(packed::Space &); }

namespace {
using sm64ds::packed::Ptr;
using sm64ds::packed::Space;
using sm64ds::Cartridge;
struct Banner { int id,x,y; };
std::vector<Banner> banners;
std::vector<int> jingles,numbers;
int roundCount=0,roundMode=-1,commits=0,records=0,newRecords=0;
bool canEnd=true;
int button=-1;
void show_banner(int id,int x,int y,int,int,short) { banners.push_back({id,x,y}); }
void play_jingle(Ptr<void> id) { jingles.push_back(static_cast<int>(id.address())); }
void observe_free(int) {}
void observe_music() {}
void observe_commit() { ++commits; }
void observe_round(Ptr<void>,Ptr<void> mode) { ++roundCount; roundMode=mode.address(); }
int query_can_end(uint32_t,int) { return canEnd; }
void observe_new_record(uint32_t) { ++newRecords; }
void native_round(uint32_t,int mode) { ++roundCount; roundMode=mode; }
int query_record() { return 0; }
int query_score() { return 1234; }
int query_language() { return 0; }
int query_top_offset() { return 32; }
int query_button(int id) { return id==button; }
unsigned observe_effect(unsigned id) { return id; }
void observe_record_table(Ptr<void>) { ++records; }
void observe_prompt(Ptr<void>,int,int) {}
void observe_sprite(Ptr<void>,int,int,Ptr<void>) {}
void observe_number(int,int,int value,int,int,int,int) { numbers.push_back(value); }

// Independent EU PMF fixture for no-ROM CI. Private-ROM execution reads all
// 49 actual descriptors instead; fixtures are never shipped as game tables.
constexpr uint32_t targets[]={
    0x020b841c,0x020b8284,0x020b6ad8,0x020b8098,0x020b7fec,0x020b6f88,
    0x020b7eac,0x020b7cd0,0x020b72d4,0x020b7c04,0x020b7b20,0x020b7fec,
    0x020b68e8,0x020b6c9c,0x020b79b0,0x020b798c,0x020b7a18,0x020b7b90,
    0x020b7e38,0x020b7f5c,0x020b81f8,0x020b83ac,0x020b853c,0x020b8688,
    0x020b7854,0x020b7744,0x020b7c04,0x020b7124,0x020b7594,0x020b7460,
    0x020b78f4,0x020b78f4,0x020b724c,0x020b6948,0x020b6ddc,0x020b70b4,
    0x020b7020,0x020b77b4,0x020b6f14,0x020b6d6c,0x020b75e4,0x020b7124,
    0x020b6c10,0x020b7fec,0x020b8560,0x020b746c,0x020b68e8,0x020b6b40,
    0x020b743c};

void exercise(Cartridge::DataImage arm9,Cartridge::DataImage common,
              Cartridge::DataImage game,bool real) {
    sm64ds::GameHeap heap(2<<20);
    Space space(heap);
    space.map(arm9.base,arm9.bytes.data(),arm9.bytes.size());
    space.map(common.base,common.bytes.data(),common.bytes.size());
    if (!game.bytes.empty()) space.map(game.base,game.bytes.data(),game.bytes.size());
    Space::Scope scope(space);
    sm64ds::minigame_sources::bind_sources(space);
    sm64ds::validate_minigame_shared_runtime(space);
    for (unsigned i=0;i<std::size(targets);++i) {
        assert(*Ptr<uint32_t>(0x020bc8bc+i*8)==targets[i]);
        assert(*Ptr<uint32_t>(0x020bc8c0+i*8)==0);
    }
    // Observe UI/audio boundaries. These are test spies, never production
    // success stubs; a full game/renderer is not claimed by this test.
    space.bind(0x020b0cac,&show_banner); space.bind(0x020ae274,&play_jingle);
    space.bind(0x020b0aa0,&observe_free); space.bind(0x020ae20c,&observe_music);
    space.bind(0x020ae2c8,&observe_music); space.bind(0x020ad90c,&observe_commit);
    space.bind(0x020b29a0,&observe_round); space.bind(0x020adbe0,&query_record);
    space.bind(0x020adbc0,&query_score); space.bind(0x020ad674,&query_language);
    space.bind(0x020b04c0,&query_top_offset); space.bind(0x020b0b1c,&query_button);
    space.bind(0x02012790,&observe_effect); space.bind(0x020b14f0,&observe_record_table);
    space.bind(0x020b0d8c,&observe_prompt); space.bind(0x020af948,&observe_sprite);
    space.bind(0x020b2444,&observe_number);
    assert(ntr::io_init());
    data_0209f5bc=&data_0209f5e8;
    data_0209d4ac=data_0209f5bc;
    data_0209f5e8.SetToStart(); data_0209f5e8.speed=0;
    const auto scene=space.allocate(0x4660),vtable=space.allocate(21*4);
    *Ptr<uint32_t>(scene)=vtable;
    *Ptr<uint32_t>(scene+0xa8)=5;
    *Ptr<uint32_t>(vtable+19*4)=0x60006000;
    *Ptr<uint32_t>(vtable+20*4)=0x60006004;
    space.bind(0x60006000,&query_can_end);
    space.bind(0x60006004,&observe_new_record);
    *Ptr<uint32_t>(0x020beb68)=scene;
    if (!real) {
        *Ptr<uint32_t>(0x020bca44)=0x020bcb00;
    }
    auto ctor=Ptr<void(uint32_t)>(0x020b8a70);
    auto enter=Ptr<void(uint32_t,int)>(0x020b87e0);
    auto behavior=Ptr<void(uint32_t)>(0x020b8778);
    auto render=Ptr<void(uint32_t)>(0x020b8714);
    auto reset=Ptr<void()>(0x020b8a8c);
    const auto state=space.allocate(0x28);
    ctor(state);
    assert(*Ptr<int>(state+0x18)==-1 && *Ptr<int>(state+0x1c)==0);
    constexpr int timers[]={180,15,180,60,120,120,120,120,0,180,
                            180,180,10,10,180,60,120,300,180,180};
    for (int id=0;id<20;++id) {
        reset(); banners.clear(); jingles.clear();
        enter(state,id);
        assert(*Ptr<int>(state+0x18)==id && *Ptr<int>(state+0x1c)==timers[id]);
        assert(*Ptr<uint32_t>(state+8)!=0);
    }
    bool rejected=false;
    try { enter(state,20); } catch (const std::out_of_range &) { rejected=true; }
    assert(rejected && *Ptr<int>(state+0x18)==19);
    rejected=false;
    try { enter(state,-1); } catch (const std::out_of_range &) { rejected=true; }
    assert(rejected && *Ptr<int>(state+0x18)==19);

    // A clear waits its complete 180-frame timer, the scene's own readiness,
    // then the real fade before delivering StartRound(0) exactly once.
    reset(); roundCount=0; canEnd=false;
    data_0209f5e8.SetToStart(); data_0209f5e8.speed=0;
    enter(state,0);
    for (int i=0;i<190;++i) behavior(state);
    assert(roundCount==0 && data_0209f5e8.IsAtStart());
    canEnd=true; behavior(state);
    assert(data_0209f5e8.speed>0 && roundCount==0);
    for (int i=0;i<40 && *Ptr<int>(state+0x18)!=-1;++i) {
        data_0209f5e8.AdvanceInterp(); behavior(state);
    }
    assert(roundCount==1 && roundMode==0 && *Ptr<int>(state+0x18)==-1);
    assert(data_0209f5e8.speed<0);
    for (int i=0;i<20;++i) behavior(state);
    assert(roundCount==1);

    // Score and result draw callbacks retain both PMF words and are dispatched
    // via the original table, rather than by an independent state switch.
    numbers.clear(); enter(state,11); render(state);
    assert(numbers==std::vector<int>{1234});
    numbers.clear(); enter(state,14); render(state);
    assert((numbers==std::vector<int>{12,3,4}));
    records=commits=0; enter(state,16); render(state);
    assert(commits==1 && records==1);
    *Ptr<int>(state+0x1c)=0;
    *Ptr<uint8_t>(0x020a0e40)=0;
    *Ptr<uint8_t>(0x020a0de8)=1; *Ptr<uint8_t>(0x020a0de9)=0;
    behavior(state); assert(*Ptr<int>(state+0x1c)==0);
    *Ptr<uint8_t>(0x020a0de9)=1;
    behavior(state); assert(*Ptr<int>(state+0x1c)==10);
    assert(*Ptr<uint32_t>(state+0x10)==0 && *Ptr<uint32_t>(state+0x14)==0);
    newRecords=0; enter(state,17);
    for (int i=0;i<120;++i) behavior(state);
    assert(newRecords==1);

    // A malformed/foreign PMF cannot execute cartridge bytes or host addresses.
    const auto saved=*Ptr<uint32_t>(0x020bc8bc);
    *Ptr<uint32_t>(0x020bc8bc)=0x020b04ec;
    rejected=false;
    try { sm64ds::validate_minigame_shared_runtime(space); }
    catch (const std::runtime_error &) { rejected=true; }
    assert(rejected); *Ptr<uint32_t>(0x020bc8bc)=saved;
    data_0209f5e8.SetToStart(); data_0209f5e8.speed=0;
    std::cout << "Original minigame controller: 20 states, timer/readiness/fade gates, PMF render, results/touch and rejected tables passed"
              << (real?" against EU cartridge.\n":" with observed UI/audio boundaries.\n");
}
}

void minigame_state_runtime() {
    Cartridge::DataImage arm9{0x02004000,std::vector<uint8_t>(0x020aa420-0x02004000)};
    Cartridge::DataImage common{0x020ad660,std::vector<uint8_t>(0x020bfec0-0x020ad660)};
    for (unsigned i=0;i<std::size(targets);++i)
        std::memcpy(common.bytes.data()+0x020bc8bc-common.base+i*8,&targets[i],4);
    exercise(std::move(arm9),std::move(common),{0,{}},false);
}
void minigame_state_cartridge(sm64ds::Cartridge &cartridge) {
    exercise(cartridge.arm9_data(0x020aa420),cartridge.overlay_data(4),
             cartridge.overlay_data(6),true);
}

void minigame_record_runtime() {
    using sm64ds::MinigameProgress;
    const auto path="/tmp/sm64ds-ranked-minigames-"+std::to_string(getpid())+".sav";
    unlink(path.c_str());
    {
        sm64ds::SaveStore store(path);
        sm64ds::GameHeap heap(2<<20);
        Space space(heap);
        space.map_owned(0x02004000,0x020aa420-0x02004000);
        space.map_owned(0x020ad660,0x020bfec0-0x020ad660);
        Space::Scope scope(space);
        sm64ds::bind_minigame_shared_runtime(space);
        const auto scene=space.allocate(0x4660);
        const auto vtable=space.allocate(21*4);
        *Ptr<uint32_t>(scene)=vtable;
        *Ptr<uint32_t>(vtable+18*4)=0x60006008;
        space.bind(0x60006008,&native_round);
        roundCount=0;
        Ptr<void(uint32_t,uint32_t)>(0x020b29a0)(scene,7);
        assert(roundCount==1 && roundMode==7);
        *Ptr<uint32_t>(0x020beb68)=scene;
        *Ptr<uint32_t>(scene+8)=0x12340600; // only bits 8..15 select save slot 6
        *Ptr<int>(0x0209b308+8)=0;
        *Ptr<int>(0x0209b308+12)=999;
        constexpr int initial[]={800,600,400,200,100};
        for (unsigned i=0;i<5;++i) MinigameProgress::set_record(6,i,initial[i]);
        *Ptr<int>(scene+0x4650)=800;
        const auto submit=Ptr<void(int)>(0x020adb1c);
        const auto commit=Ptr<void()>(0x020ad90c);
        const auto score=Ptr<int()>(0x020adbc0);
        const auto best=Ptr<int()>(0x020adafc);
        const auto isNew=Ptr<int()>(0x020adbe0);
        assert(Ptr<int(uint32_t)>(0x020adc3c)(scene)==6);
        submit(1234);
        assert(score()==999 && best()==999 && isNew()==1);
        commit();
        constexpr int ranked[]={999,800,600,400,200};
        for (unsigned i=0;i<5;++i) assert(MinigameProgress::record(6,i)==ranked[i]);
        assert(*Ptr<int>(scene+0x64)==0 && *Ptr<int>(scene+0x4658)==999);
        assert(best()==0 && isNew()==0 && *Ptr<uint8_t>(0x0209b300)==1);
        submit(850); commit();
        assert(*Ptr<int>(scene+0x64)==1 && MinigameProgress::record(6,1)==850);
        assert(MinigameProgress::persist());

        // The original lower-is-better mode intentionally ranks session best
        // but inserts the last submitted score; keep that distinction intact.
        *Ptr<uint32_t>(scene+8)=7u<<8;
        *Ptr<int>(0x0209b308+8)=5;
        for (unsigned i=0;i<5;++i) MinigameProgress::set_record(7,i,1000+int(i)*500);
        *Ptr<int>(scene+0x4650)=1000;
        submit(900); submit(950);
        assert(best()==900 && score()==950 && isNew()==1);
        commit();
        assert(MinigameProgress::record(7,0)==950 && MinigameProgress::record(7,1)==1000);
        assert(*Ptr<int>(scene+0x4658)==900 && best()==0);
        submit(0); commit();
        assert(*Ptr<int>(scene+0x64)==-1 && MinigameProgress::record(7,0)==950);
        assert(MinigameProgress::persist());
        *Ptr<uint32_t>(0x020beb68)=0;
        assert(score()==0 && best()==0 && isNew()==0);
        submit(777); commit();
    }
    {
        sm64ds::SaveStore store(path);
        assert(MinigameProgress::record(6,0)==999 && MinigameProgress::record(6,1)==850);
        assert(MinigameProgress::record(7,0)==950 && MinigameProgress::record(7,1)==1000);
    }
    unlink(path.c_str());
    std::cout << "Original minigame scoring: cap, record-slot extraction, session/submission distinction, ranking, EEPROM persist/reopen passed.\n";
}

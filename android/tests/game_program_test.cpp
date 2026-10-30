#include "runtime/game_program.h"
#include "runtime/resources.h"
#include "runtime/save_store.h"
#include "pc_audio/stream.h"
#include "runtime/boot_seams.h"
#include "runtime/packed_ptr.h"
#include "runtime/packed_fs.h"
#include "runtime/packed_audio.h"
#include "runtime/stage_runtime.h"
#include "ntr/lz77.h"
#include "ntr/fs.h"
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <unistd.h>
extern "C" void MultiCopy_Int(int *, int *, int);
namespace cstd {
s16 atan2(s32, s32);
int sqrt(unsigned long long);
s32 fdiv(s32, s32);
}
namespace sm64ds::frontend { void bind_arm9_sources(packed::Space &); }
namespace sm64ds::stage_sources { void bind_sources(packed::Space &); }
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

unsigned overlayInitializers=0;
void synthetic_initializer() {
    ++overlayInitializers;
    *sm64ds::packed::Ptr<uint32_t>(0x020b0004u)=0x55667788u;
}
std::vector<int> selectedOverlays;
void collect_overlay(int id) { selectedOverlays.push_back(id); }

void overlay_sdk() {
    using sm64ds::packed::Ptr;
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u),card(0x300);
    space.map(0x02004000u,main.data(),main.size());
    space.map(0x08000000u,card.data(),card.size());
    space.map(0x027ffe00u,card.data(),0x160);
    space.map_owned(0x027ffc40u,2);
    sm64ds::frontend::bind_arm9_sources(space);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_stage_runtime(space);
    sm64ds::bind_packed_fs(space);
    space.bind(0x020b0020u,&synthetic_initializer);
    space.bind(0x60001000u,&collect_overlay);
    sm64ds::packed::Space::Scope scope(space);
    // Synthetic card: one uncompressed overlay, one initializer word and a
    // nonempty BSS. Every SDK table/file operation below is original code.
    put32(card,0x50,0x100); put32(card,0x54,32);
    put32(card,0x100,0); put32(card,0x104,0x020b0000u);
    put32(card,0x108,64); put32(card,0x10c,32);
    put32(card,0x110,0x020b0010u); put32(card,0x114,0x020b0014u);
    put32(card,0x118,0); put32(card,0x11c,0);
    put32(card,0x200,0x280); put32(card,0x204,0x2c0);
    std::fill(card.begin()+0x280,card.begin()+0x2c0,0xa5);
    put32(card,0x290,0x020b0020u);
    const uint32_t archive=0x020a8074u;
    *Ptr<uint32_t>(archive+0x10)=2;
    *Ptr<uint32_t>(archive+0x1c)=0x08000000u;
    *Ptr<uint32_t>(archive+0x20)=0x200;
    *Ptr<uint32_t>(archive+0x24)=8;
    Ptr<void()>(0x02017e60u)();
    const uint32_t info=space.allocate(0x24);
    assert(Ptr<int(uint32_t,int,int)>(0x02018c00u)(info,0,0)==1);
    assert(std::memcmp(space.data(info,32),card.data()+0x100,32)==0);
    assert(*Ptr<uint32_t>(info+32)==0);
    assert(Ptr<int(uint32_t,int,int)>(0x02018c00u)(info,0,1)==0);
    // Also exercise the SDK's cached table path, including the final entry.
    *Ptr<uint32_t>(0x020a8064u)=0x08000100u;
    *Ptr<uint32_t>(0x020a8068u)=32;
    std::memset(space.data(info,0x24),0xa5,0x24);
    assert(Ptr<int(uint32_t,int,int)>(0x02018c00u)(info,0,0)==1);
    assert(std::memcmp(space.data(info,32),card.data()+0x100,32)==0);
    assert(Ptr<int(uint32_t,int,int)>(0x02018c00u)(info,0,1)==0);
    *Ptr<uint32_t>(0x020a8064u)=0;
    overlayInitializers=0;
    Ptr<void(int)>(0x02018028u)(0);
    assert(overlayInitializers==1 && space.contains(0x020b0000u,96));
    assert(!space.contains(0x020b0000u,97) && !space.contains(0x020b0000u-1,1));
    assert(*Ptr<uint32_t>(0x020b0004u)==0x55667788u);
    for(unsigned i=64;i<96;++i) assert(*Ptr<uint8_t>(0x020b0000u+i)==0);
    assert(*Ptr<uint32_t>(0x0209d3c4u)==0);
    assert(*Ptr<uint32_t>(0x0209d3c8u)==0x020b0000u);
    assert(*Ptr<uint32_t>(0x0209d3ccu)==96);
    bool overlap=false;
    try { Ptr<void(int)>(0x02018028u)(0); }
    catch(const std::runtime_error &e) {
        overlap=std::string(e.what())=="Original game graphics invariant failed.";
    }
    assert(overlap && overlayInitializers==1);
    const uint32_t shared=space.allocate(8);
    const auto before=space.allocations();
    const uint32_t loaded=Ptr<uint32_t(uint32_t)>(0x02017bc4u)(shared);
    assert(loaded && space.allocations()==before+1 && space.allocation_size(loaded)==64);
    assert(std::memcmp(space.data(loaded,64),card.data()+0x280,64)==0);
    assert(*Ptr<uint8_t>(shared+2)==1 && *Ptr<uint32_t>(shared+4)==loaded);
    assert(Ptr<uint32_t(uint32_t)>(0x02017bc4u)(shared)==loaded);
    assert(*Ptr<uint8_t>(shared+2)==2 && space.allocations()==before+1);
    *Ptr<uint8_t>(shared+2)=0xff;
    assert(Ptr<uint32_t(uint32_t)>(0x02017bc4u)(shared)==0);
    *Ptr<uint8_t>(shared+2)=2;
    Ptr<void(uint32_t)>(0x02017b64u)(shared);
    assert(*Ptr<uint8_t>(shared+2)==1 && *Ptr<uint32_t>(shared+4)==loaded);
    Ptr<void(uint32_t)>(0x02017b64u)(shared);
    assert(*Ptr<uint8_t>(shared+2)==0 && *Ptr<uint32_t>(shared+4)==0);
    assert(space.allocations()==before);
    space.release(shared);
    // The real seven-column object table and its extra shared overlays.
    for(unsigned i=0;i<7;++i) {
        *Ptr<uint8_t>(0x02075998u+7+i)=1;
        *Ptr<uint32_t>(0x02075804u+i*28+4)=40+i;
    }
    selectedOverlays.clear();
    Ptr<void(uint32_t,int)>(0x0202df70u)(0x60001000u,1);
    assert((selectedOverlays==std::vector<int>{40,41,42,43,44,45,46,98,102}));
    selectedOverlays.clear();
    Ptr<void(uint32_t,int)>(0x0202df70u)(0x60001000u,0x24);
    assert((selectedOverlays==std::vector<int>{60}));
    *Ptr<uint8_t>(0x0209f2d8u)=1;
    selectedOverlays.clear();
    Ptr<void(uint32_t,int)>(0x0202df70u)(0x60001000u,1);
    assert(selectedOverlays.empty());
    space.release(info);
    std::cout << "Original overlay SDK: card/cached metadata, image, BSS, initializer, overlap and selection passed.\n";
}

void audio_groups() {
    using sm64ds::packed::Ptr;
    std::vector<uint8_t> sound(0x100),main(0x020aa420u-0x02004000u);
    std::memcpy(sound.data(),"SDAT",4);
    put32(sound,0x18,0x40); put32(sound,0x1c,0x40);
    put32(sound,0x20,0x80); put32(sound,0x24,12);
    std::memcpy(sound.data()+0x40,"INFO",4);
    std::memcpy(sound.data()+0x80,"FAT ",4);
    put32(sound,0x5c,40); put32(sound,0x68,1); put32(sound,0x6c,48);
    // A genuine empty common group is legal in the SDK. No sound files or
    // game assets are needed to exercise its native cache save/restore owner.
    pc_archive_load_image(std::move(sound));
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    space.map(0x02004000u,main.data(),main.size());
    space.map_owned(0x027ffc40u,2);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_packed_audio(space);
    sm64ds::initialize_packed_audio(space);
    sm64ds::packed::Space::Scope scope(space);
    const auto owner=*Ptr<uint32_t>(0x0209b498u);
    assert(owner && *Ptr<uint32_t>(0x0209b4a8u)==1);
    assert(*Ptr<uint8_t>(0x0208e428u)==0x20);
    assert(Ptr<int(uint32_t)>(0x0205117cu)(owner)==2);
    assert(Ptr<int(uint32_t,int)>(0x020510a4u)(owner,1)==1);
    assert(Ptr<int(uint32_t,int)>(0x020510a4u)(owner,2)==0);
    bool rejected=false;
    try { Ptr<int(uint32_t,uint32_t)>(0x020134d8u)(1,owner); }
    catch(const std::runtime_error &) { rejected=true; }
    assert(rejected);
    pc_archive_close();
    std::cout << "Original sound-group loader: native SDAT owner, save/restore and missing-group rejection passed.\n";
}

void controller_glyph_words() {
    using sm64ds::packed::Ptr;
    assert(ntr::io_init());
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u);
    space.map(0x02004000u,main.data(),main.size());
    space.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    space.map(ntr::VRAM_BASE,ntr::host_ptr(ntr::VRAM_BASE),ntr::VRAM_SIZE);
    sm64ds::frontend::bind_arm9_sources(space);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_stage_runtime(space);
    sm64ds::packed::Space::Scope scope(space);
    ntr::io_write(0x04000240u,0x81,1);
    ntr::io_write(0x04000000u,0,4);
    ntr::io_write(0x0400000cu,0,2);
    ntr::io_write(0x0400000eu,0,2);
    const uint32_t text=space.allocate(2),font=0x0600a040u,target=0x06005020u;
    *Ptr<uint8_t>(text)=0x41; *Ptr<uint8_t>(text+1)=0xff;
    *Ptr<uint32_t>(0x0209d6f4u)=text;
    *Ptr<uint8_t>(0x0209d65cu)=8;
    *Ptr<uint8_t>(0x0209d6b0u)=12;
    *Ptr<uint8_t>(0x0208f174u+0x41)=12;
    for(unsigned row=0;row<16;++row) {
        const uint32_t source=font+(row/8)*0x400+(row%8)*4;
        *Ptr<uint32_t>(source)=0xf1234567u;
        *Ptr<uint32_t>(source+0x20)=0x12345678u;
    }
    std::memset(space.data(target,128),0,128);
    Ptr<void(int)>(0x0201b100u)(1);
    assert(*Ptr<uint32_t>(target)==0xf1234567u);
    assert(*Ptr<uint32_t>(target+32)==0x12345678u);
    assert(*Ptr<uint8_t>(0x0209d65cu)==8);
    assert(*Ptr<uint32_t>(0x0209d6f4u)==text+1);
    space.release(text);
    std::cout << "Original glyph renderer: signed pixel words and logical shift by 32 passed.\n";
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
    overlay_sdk();
    audio_groups();
    controller_glyph_words();
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
            bool unloadedInitializer=false;
            try { (void)stageSpace->code(0x02100560u); }
            catch(const std::runtime_error &) { unloadedInitializer=true; }
            assert(unloadedInitializer); // data residency alone must not publish module code
            try {
                game.tick();
            } catch (const std::runtime_error &e) {
                reachedStage = std::string(e.what()) ==
                    "Unlinked native code handle: " + std::to_string(0x02073470u);
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
            // All eleven actual modules and all 58 static initializers ran.
            // Code stays the real BLZ result. Constructors build handles and
            // state tables in writable data/BSS instead of fabricated data.
            const unsigned modules[]={1,2,9,62,69,85,89,94,100,98,102};
            for(unsigned i=0;i<11;++i) {
                ntr::OverlayInfo info{};
                assert(ntr::fs_overlay_info(modules[i],info));
                const auto expectedImage=cartridge.overlay_data(modules[i]);
                const uint32_t table=0x08000000u+stageWord(0x027ffe50u)+modules[i]*32;
                const uint32_t codeEnd=stageWord(table+0x10);
                assert(codeEnd>=info.ramAddress && codeEnd-info.ramAddress<=info.ramSize);
                const uint32_t codeBytes=codeEnd-info.ramAddress;
                assert(std::memcmp(stageSpace->data(expectedImage.base,codeBytes),
                                   expectedImage.bytes.data(),codeBytes)==0);
                const uint32_t region=0x0209d3c4u+i*12;
                assert(stageWord(region)==modules[i]);
                assert(stageWord(region+4)==info.ramAddress);
                assert(stageWord(region+8)==info.ramSize+info.bssSize);
            }
            assert(stageWord(0x0209d3c4u+11*12)==UINT32_MAX);
            assert(stageWord(0x02092130u)==9);
            assert(*static_cast<uint16_t *>(stageSpace->data(0x0209d6d4u,2))==0x282);
            assert(*static_cast<uint8_t *>(stageSpace->data(0x0209d6b0u,1))>0);
            assert(stageWord(0x0209d6fcu)==stageSpace->address(data_0209d6fc));
            assert(*static_cast<uint8_t *>(stageSpace->data(0x0209b47cu,1))==
                   *static_cast<uint8_t *>(stageSpace->data(0x02075768u+3,1)));
            assert(*static_cast<uint8_t *>(stageSpace->data(0x0208e428u,1))==
                   *static_cast<uint8_t *>(stageSpace->data(0x02075769u+3,1)));
            for(auto handle : {std::pair<uint32_t,uint32_t>{0x0210da18u,0x3c7},
                               {0x02113c20u,0x438},{0x02113c28u,0x439},
                               {0x02113c68u,0x693},{0x02113c70u,0x694},
                               {0x02148934u,0x4a1}}) {
                assert(*static_cast<uint16_t *>(stageSpace->data(handle.first,2))==
                       resources.file_id(handle.second));
                if (handle.first==0x0210da18u || handle.first>=0x02113c20u) {
                    assert(*static_cast<uint8_t *>(stageSpace->data(handle.first+2,1))==0);
                    assert(stageWord(handle.first+4)==0);
                }
            }
            for(auto pair : {std::pair<uint32_t,uint32_t>{0x0210db80u,0x021081b8u},
                             {0x021097bcu,0x02109758u},
                             {0x02148974u,0x02086b58u},{0x0214897cu,0x0214836cu}})
                assert(std::memcmp(stageSpace->data(pair.first,8),stageSpace->data(pair.second,8),8)==0);
            std::unordered_set<uint32_t> destructors;
            for(uint32_t node=stageWord(0x020aa3f0u);node;node=stageWord(node)) {
                assert(destructors.insert(node).second && destructors.size()<2000);
                const uint32_t callback=stageWord(node+4);
                assert(callback==0x02017ab4u || callback==0x02017984u ||
                       callback==0x02017b34u || callback==0x0201791cu ||
                       callback==0x020072c0u || callback==0x0201973cu);
                assert(stageSpace->contains(stageWord(node+8),1));
            }
            assert(destructors.size()>100);
            assert(stageWord(0x0210da94u)==0x02017ab4u && stageWord(0x0210da98u)==0x0210da18u);
            assert(stageSpace->code(0x02100560u) && stageSpace->code(0x02112458u));
            assert(stageWord(0x0209cef8u)==12);
            for(unsigned i=0;i<12;++i) {
                const uint32_t holder=stageWord(0x020756f0u+i*4),file=stageWord(holder+4);
                assert(*static_cast<uint8_t *>(stageSpace->data(holder+2,1))==1);
                assert(file && stageSpace->allocation_size(file)>=0x40);
                assert(stageWord(file+8)>=file); // original BMD offsets became checked pointers
                assert(stageWord(0x0209cefc+i*12)==file);
            }
            {
                sm64ds::packed::Space::Scope scope(*stageSpace);
                const uint32_t file=sm64ds::packed::Ptr<uint32_t(uint32_t)>(0x0201816cu)(0x8006);
                const auto expectedFile=cartridge.file(0x8006);
                assert(stageSpace->allocation_size(file)>=expectedFile.size());
                assert(std::memcmp(stageSpace->data(file,expectedFile.size()),expectedFile.data(),expectedFile.size())==0);
                stageSpace->release(file);
            }
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

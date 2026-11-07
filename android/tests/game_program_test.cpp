#include "runtime/game_program.h"
#include "runtime/minigame_progress.h"
#include "runtime/minigame_art.h"
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
#include "ntr/gx.h"
#include "stage_code_ranges.h"
#include <cassert>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <unistd.h>
extern "C" void MultiCopy_Int(int *, int *, int);
extern "C" void *Mg_GetWorkBuffer();
extern "C" int LoadFileAt(int, void *);
namespace cstd {
s16 atan2(s32, s32);
int sqrt(unsigned long long);
s32 fdiv(s32, s32);
}
namespace sm64ds::frontend { void bind_arm9_sources(packed::Space &); }
namespace sm64ds::stage_sources {
void bind_sources(packed::Space &);
void bind_overlay_sources(packed::Space &,unsigned);
}
void minigame_oam_runtime();
void minigame_state_runtime();
void minigame_base_leaf_runtime();
void minigame_constructor_runtime();
void minigame_record_runtime();
void wipe_runtime_tests();
void minigame_state_cartridge(sm64ds::Cartridge &);
void minigame_gameplay_cartridge(const char *);
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
unsigned arrayConstructed=0;
unsigned arrayThrowAt=0;
std::vector<unsigned> arrayDestroyed;
void construct_array_element(uint32_t element) {
    if (++arrayConstructed==arrayThrowAt) throw std::runtime_error("array construction test");
    *sm64ds::packed::Ptr<uint32_t>(element)=arrayConstructed;
}
void destroy_array_element(uint32_t element) {
    arrayDestroyed.push_back(*sm64ds::packed::Ptr<uint32_t>(element));
}
bool allowStateCleanup=false;
unsigned stateCleanups=0,stateInitializations=0;
int cleanup_player_state(uint32_t) { ++stateCleanups; return allowStateCleanup; }
int initialize_player_state(uint32_t) { ++stateInitializations; return 1; }

void player_state_runtime() {
    using sm64ds::packed::Ptr;
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u);
    space.map(0x02004000u,main.data(),main.size());
    space.map_owned(0x02110000u,0x2000);
    sm64ds::bind_stage_runtime(space);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::stage_sources::bind_overlay_sources(space,2);
    sm64ds::bind_stage_overlay_runtime(space,2);
    space.bind(0x60003000u,&cleanup_player_state);
    space.bind(0x60003001u,&initialize_player_state);
    sm64ds::packed::Space::Scope scope(space);
    const uint32_t player=space.allocate(0x768),previous=space.allocate(24),next=space.allocate(24);
    *Ptr<uint32_t>(previous+16)=0x60003000u;
    *Ptr<uint32_t>(next)=0x60003001u;
    *Ptr<uint32_t>(player+0x370)=previous;
    *Ptr<uint32_t>(player+0x628)=123;
    *Ptr<uint16_t>(player+0x6ce)=0x280;
    auto change=Ptr<int(uint32_t,uint32_t)>(0x020e30a0u);
    allowStateCleanup=false; stateCleanups=stateInitializations=0;
    assert(change(player,next)==0 && stateCleanups==1 && stateInitializations==0);
    assert(*Ptr<uint32_t>(player+0x370)==previous && *Ptr<uint32_t>(player+0x378)==next);
    assert(*Ptr<uint32_t>(player+0x628)==123);
    allowStateCleanup=true;
    assert(change(player,next)==1 && stateCleanups==2 && stateInitializations==1);
    assert(*Ptr<uint32_t>(player+0x374)==previous && *Ptr<uint32_t>(player+0x370)==next);
    assert(*Ptr<uint32_t>(player+0x628)==0 && *Ptr<uint16_t>(player+0x6ce)==0x80);
    assert(*Ptr<int32_t>(player+0x9c)==-0x4000 && *Ptr<int32_t>(player+0xa0)==-0x4b000);
    *Ptr<uint32_t>(player+0x370)=0x0211022cu;
    *Ptr<uint8_t>(player+0x709)=1;
    *Ptr<uint32_t>(player+0x2ec)=4;
    assert(change(player,next)==0 && *Ptr<uint32_t>(player+0x370)==0x0211022cu);
    assert(change(player,0x0211013cu)==1);
    assert(*Ptr<uint8_t>(player+0x709)==0 && *Ptr<uint32_t>(player+0x2ec)==0);
    *Ptr<uint32_t>(player+0x358)=123;
    assert(change(player,0x0211013cu)==1);
    assert(*Ptr<uint32_t>(player+0x370)==0x02110364u && *Ptr<uint8_t>(player+0x6e3)==1);
    assert(*Ptr<uint32_t>(player+0x378)==0x0211013cu);
    space.release(next); space.release(previous); space.release(player);
    assert(space.allocations()==0);
    std::cout << "Original Player state transition: cleanup veto, native PMFs, reset, control lock and carried-object redirect passed.\n";
}

void item_block_state_runtime() {
    using sm64ds::packed::Ptr;
    sm64ds::GameHeap heap(1024*1024);
    sm64ds::packed::Space space(heap);
    space.map_owned(0x0214e890u,48);
    sm64ds::stage_sources::bind_overlay_sources(space,102);
    sm64ds::bind_stage_overlay_runtime(space,102);
    sm64ds::packed::Space::Scope scope(space);
    const uint32_t block=space.allocate(0x3f8);
    const sm64ds::packed::MemberCall idle{0x02149d80u,0};
    std::memcpy(space.data(0x0214e890u,8),&idle,8);
    *Ptr<int32_t>(block+0x3dc)=0x4242;
    *Ptr<int32_t>(block+0x80)=*Ptr<int32_t>(block+0x84)=*Ptr<int32_t>(block+0x88)=7;
    Ptr<void(uint32_t,int)> enter(0x02149da8u);
    enter(block,0);
    assert(*Ptr<int32_t>(block+0x3e8)==0 && *Ptr<int32_t>(block+0x3dc)==0);
    for(unsigned offset : {0x80u,0x84u,0x88u}) assert(*Ptr<int32_t>(block+offset)==0x1000);
    assert(*Ptr<int32_t>(block+0x9c)==-0x8000);
    const sm64ds::packed::MemberCall missing{0x60005000u,0};
    std::memcpy(space.data(0x0214e890u,8),&missing,8);
    bool rejected=false;
    try { enter(block,0); } catch(const std::runtime_error &) { rejected=true; }
    assert(rejected);
    std::cout << "Original item-block state: enter PMF, idle scale/gravity reset and unknown-callback rejection passed.\n";
}

void texture_animation_runtime() {
    using sm64ds::packed::Ptr;
    sm64ds::GameHeap heap(1024*1024);
    sm64ds::packed::Space space(heap);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_stage_runtime(space);
    sm64ds::packed::Space::Scope scope(space);
    const uint32_t control=space.allocate(20),anim=space.allocate(2),other=space.allocate(2);
    *Ptr<uint16_t>(anim)=3; *Ptr<uint16_t>(other)=2;
    Ptr<void(uint32_t,uint32_t,int,int,uint16_t)> setFile(0x020158acu);
    Ptr<void(uint32_t)> advance(0x02015c3cu);
    setFile(control,anim,0,0x1000,1);
    assert(*Ptr<int32_t>(control+8)==0x1000);
    advance(control); assert(*Ptr<int32_t>(control+8)==0x2000);
    advance(control); assert(*Ptr<int32_t>(control+8)==0);
    // Reusing the same BTA preserves its cursor while replacing flags/rate.
    setFile(control,anim,0,-0x1000,2);
    assert(*Ptr<int32_t>(control+8)==0);
    advance(control); assert(*Ptr<int32_t>(control+8)==0x2000);
    setFile(control,anim,0x40000000,0x1000,0);
    advance(control); assert(*Ptr<int32_t>(control+8)==0x2fff);
    setFile(control,anim,0x40000000,-0x4000,0);
    advance(control); assert(*Ptr<int32_t>(control+8)==0);
    setFile(control,other,0,0x800,0);
    assert(*Ptr<uint32_t>(control+4)==0x2000 && *Ptr<int32_t>(control+8)==0);
    advance(control); assert(*Ptr<int32_t>(control+8)==0x800);
    std::cout << "Original texture animation: file changes, retained cursors, forward/reverse wrapping and one-shot clamping passed.\n";
}

void moving_mesh_runtime() {
    using sm64ds::packed::Ptr;
    assert(ntr::io_init());
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u);
    space.map(0x02004000u,main.data(),main.size());
    space.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    sm64ds::frontend::bind_arm9_sources(space);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_stage_runtime(space);
    sm64ds::packed::Space::Scope scope(space);
    const uint32_t collider=space.allocate(0x1c8),matrix=space.allocate(48);
    const uint32_t file=space.allocate(0x38),clps=space.allocate(16);
    const uint32_t input=space.allocate(12),output=space.allocate(12);
    sm64ds::native_mesh_collider_construct(collider);
    *Ptr<uint32_t>(collider)=0x02099434u;
    *Ptr<int32_t>(matrix)=*Ptr<int32_t>(matrix+16)=*Ptr<int32_t>(matrix+32)=0x1000;
    *Ptr<int32_t>(matrix+36)=0xa000;
    *Ptr<int32_t>(matrix+40)=0x14000;
    *Ptr<int32_t>(matrix+44)=-0x5000;
    Ptr<void(uint32_t,uint32_t,uint32_t,int,int16_t,uint32_t)>(0x0203a1e0u)(
        collider,file,matrix,0x2000,0x7f00,clps);
    assert(*Ptr<int32_t>(collider+0x50)==0x2000 && *Ptr<int32_t>(collider+0x164)==0x800);
    assert(*Ptr<uint32_t>(collider+0x20)==file && *Ptr<uint32_t>(collider+0x24)==clps);
    *Ptr<int32_t>(input)=0x1000;
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02039e30u)(collider,input,output);
    assert(*Ptr<int32_t>(output)==0xc000 && *Ptr<int32_t>(output+4)==0x14000);
    assert(*Ptr<int32_t>(output+8)==-0x5000);
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02039e48u)(collider,output,output);
    assert(*Ptr<int32_t>(output)==0x1000 && *Ptr<int32_t>(output+4)==0 && *Ptr<int32_t>(output+8)==0);
    // A quarter-turn and translation must carry an existing world-space point
    // using the previous inverse, independently of the uniform mesh scale.
    *Ptr<int32_t>(matrix)=*Ptr<int32_t>(matrix+32)=0;
    *Ptr<int32_t>(matrix+8)=-0x1000; *Ptr<int32_t>(matrix+24)=0x1000;
    *Ptr<int32_t>(matrix+36)=0xe000; *Ptr<int32_t>(matrix+40)=0x16000;
    *Ptr<int32_t>(matrix+44)=-0x4000;
    Ptr<void(uint32_t,uint32_t,int16_t)>(0x02039f20u)(collider,matrix,int16_t(0x8100));
    assert(Ptr<int16_t(uint32_t)>(0x02039924u)(collider)==0x200);
    Ptr<void(uint32_t,uint32_t)>(0x02039908u)(collider,output);
    assert(*Ptr<int32_t>(output)==0x4000 && *Ptr<int32_t>(output+4)==0x2000);
    assert(*Ptr<int32_t>(output+8)==0x1000);
    *Ptr<int32_t>(input)=0xb000; *Ptr<int32_t>(input+4)=0x14000;
    *Ptr<int32_t>(input+8)=-0x5000;
    assert(Ptr<int(uint32_t,uint32_t,uint32_t)>(0x02039930u)(collider,input,output)==1);
    assert(*Ptr<int32_t>(output)==0xe000 && *Ptr<int32_t>(output+4)==0x16000);
    assert(*Ptr<int32_t>(output+8)==-0x5000);
    Ptr<void(uint32_t)>(0x020398fcu)(collider);
    Ptr<void(uint32_t,uint32_t,int16_t)>(0x02039f20u)(collider,matrix,int16_t(0x8100));
    assert(*Ptr<uint8_t>(collider+0x34)==1); // stationary movement filter
    *Ptr<int32_t>(matrix+36)+=0x1000;
    Ptr<void(uint32_t,uint32_t,int16_t)>(0x02039f20u)(collider,matrix,int16_t(0x8100));
    assert(*Ptr<uint8_t>(collider+0x34)==0 && *Ptr<uint8_t>(collider+0x35)==1);
    assert(*Ptr<int32_t>(collider+0x44)==0xe66);
    for(uint32_t address : {output,input,clps,file,matrix,collider}) space.release(address);
    assert(space.allocations()==0);
    std::cout << "Original moving mesh: scale/inverse, rotation, carrying, velocity, yaw wrap and motion filter passed.\n";
}

void ground_runtime() {
    using sm64ds::packed::Ptr;
    assert(ntr::io_init());
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u);
    space.map(0x02004000u,main.data(),main.size());
    space.map(ntr::IO_BASE,ntr::host_ptr(ntr::IO_BASE),ntr::IO_SIZE);
    sm64ds::frontend::bind_arm9_sources(space);
    sm64ds::stage_sources::bind_sources(space);
    sm64ds::bind_stage_runtime(space);
    sm64ds::packed::Space::Scope scope(space);
    const uint32_t file=space.allocate(0x100),collider=space.allocate(0x50),
        query=space.allocate(0x50),line=space.allocate(0x78),
        position=space.allocate(12),end=space.allocate(12),owner=space.allocate(0xd4);
    // One floor prism in a terminal octree cell, with genuine KCL scales.
    for (auto lane : {std::pair<unsigned,uint32_t>{0,0x40},{4,0x60},{8,0x90},{12,0xc0},
                      {0x20,~3u},{0x24,~3u},{0x28,~3u},{0x2c,2},
                      {0xa0,0x100000},{0xc0,0x80000004u}})
        *Ptr<uint32_t>(file+lane.first)=lane.second;
    const int16_t normals[]={0,1024,0,-1024,0,0,0,0,-1024,1024,0,1024};
    std::memcpy(space.data(file+0x60,sizeof normals),normals,sizeof normals);
    *Ptr<uint16_t>(file+0xa8)=1;*Ptr<uint16_t>(file+0xaa)=2;*Ptr<uint16_t>(file+0xac)=3;
    *Ptr<uint16_t>(file+0xc6)=1;
    *Ptr<uint16_t>(file+0xe4)=8;
    sm64ds::native_mesh_collider_construct(collider);
    *Ptr<uint32_t>(0x020993dcu+16)=0x01ffd8d8u;
    *Ptr<uint32_t>(0x020993dcu+24)=0x01ffd3f8u;
    *Ptr<uint32_t>(0x020993dcu+28)=0x01ffb0fcu;
    Ptr<void(uint32_t)>(0x02039760u)(file);
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x020396f0u)(collider,file,file+0xe0);
    *Ptr<uint32_t>(owner+4)=7;
    Ptr<void(uint32_t,uint32_t)>(0x02039184u)(collider,owner);
    Ptr<uint32_t(uint32_t)>(0x02037570u)(query);
    *Ptr<int32_t>(position)=4096;*Ptr<int32_t>(position+4)=8192;*Ptr<int32_t>(position+8)=4096;
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203748cu)(query,position,0);
    auto detect=Ptr<int(uint32_t)>(0x02038f44u);
    assert(detect(query)==1 && *Ptr<int32_t>(query+0x44)==0);
    assert(*Ptr<uint8_t>(query+0x48)==1 && *Ptr<uint16_t>(query+0x28)==1);
    assert(*Ptr<int32_t>(query+0x20)==4096); // Q10 file normal becomes Q12
    assert(*Ptr<uint32_t>(query+0x30)==owner && *Ptr<uint32_t>(query+0x34)==collider);
    // Water is independently filtered; an owned collider is never hit.
    *Ptr<uint32_t>(file+0xe8)=0x20;
    assert(detect(query)==0 && *Ptr<int32_t>(query+0x44)==INT_MIN);
    Ptr<void(uint32_t)>(0x0203549cu)(query);
    assert(detect(query)==1);
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203748cu)(query,position,owner);
    assert(detect(query)==0);
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203748cu)(query,position,0);
    *Ptr<int32_t>(position)=-4096;
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203748cu)(query,position,0);
    assert(detect(query)==0); // outside the octree, including its negative side
    // A descending segment crosses that same prism at the same height.
    *Ptr<uint32_t>(file+0xe8)=0;
    *Ptr<int32_t>(position)=4096;
    *Ptr<int32_t>(end)=4096;*Ptr<int32_t>(end+4)=-8192;*Ptr<int32_t>(end+8)=4096;
    Ptr<uint32_t(uint32_t)>(0x020377b0u)(line);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(line,position,end,0);
    assert(Ptr<bool(uint32_t)>(0x02038638u)(line));
    // The cartridge shifts Vec3_Dist's result by six before storing this
    // lane (ITCM 0x01ffb6a4); preserve that raw distance convention.
    assert(*Ptr<int32_t>(line+0x58)==0 && *Ptr<int32_t>(line+0x60)==128);
    assert(*Ptr<uint8_t>(line+0x50)==1 && *Ptr<uint16_t>(line+0x28)==1);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(line,end,position,0);
    assert(!Ptr<bool(uint32_t)>(0x02038638u)(line)); // one-sided surface
    for (uint32_t allocation : {owner,end,position,line,query,collider,file}) space.release(allocation);
    assert(space.allocations()==0);
    std::cout << "Original KCL rays: ground/segment octrees, floor height, hit records, water/owner filters and one-sided surfaces passed.\n";
}

std::vector<std::pair<uint32_t,uint32_t>> actorPhases;
int record_actor_phase(uint32_t actor) {
    using sm64ds::packed::Ptr;
    assert(*Ptr<uint32_t>(0x020a4b68u));
    actorPhases.emplace_back(*Ptr<uint32_t>(0x02099f24u),actor);
    return 1;
}
void record_tree_phase(uint32_t actor) {
    actorPhases.emplace_back(*sm64ds::packed::Ptr<uint32_t>(0x02099f24u),actor);
}
void actor_frame_runtime() {
    using sm64ds::packed::Ptr;
    sm64ds::GameHeap heap(2*1024*1024);
    sm64ds::packed::Space space(heap);
    std::vector<uint8_t> main(0x020aa420u-0x02004000u);
    space.map(0x02004000u,main.data(),main.size());
    sm64ds::bind_stage_runtime(space);
    space.bind(0x60004000u,&record_actor_phase);space.bind(0x60004001u,&record_tree_phase);
    sm64ds::packed::Space::Scope scope(space);
    std::vector<uint32_t> allocated;
    std::vector<std::pair<uint32_t,uint32_t>> expected;
    for (auto phase : {std::pair<uint32_t,uint32_t>{4,0x020a4ba8u},
                       {2,0x020a4b88u},{3,0x020a4b78u},{5,0x020a4b98u}}) {
        const uint32_t actor=space.allocate(0x50),node=actor+0x28;allocated.push_back(actor);
        *Ptr<uint32_t>(node+8)=actor;
        *Ptr<uint32_t>(phase.second)=*Ptr<uint32_t>(phase.second+4)=node;
        *Ptr<uint32_t>(phase.second+8)=0x60004000u;
        expected.emplace_back(phase.first,actor);
    }
    const uint32_t rootActor=space.allocate(0x50),childActor=space.allocate(0x50),siblingActor=space.allocate(0x50);
    const uint32_t root=rootActor+0x14,child=childActor+0x14,sibling=siblingActor+0x14;
    allocated.insert(allocated.end(),{rootActor,childActor,siblingActor});
    *Ptr<uint32_t>(root+4)=child;*Ptr<uint32_t>(root+16)=rootActor;
    *Ptr<uint32_t>(child)=root;*Ptr<uint32_t>(child+12)=sibling;*Ptr<uint32_t>(child+16)=childActor;
    *Ptr<uint32_t>(sibling)=root;*Ptr<uint32_t>(sibling+16)=siblingActor;
    *Ptr<uint32_t>(0x020a4b6cu)=root;*Ptr<uint32_t>(0x020a4b70u)=0x60004001u;
    expected.insert(expected.end(),{{1,rootActor},{1,childActor},{1,siblingActor}});
    actorPhases.clear();
    Ptr<void()>(0x02044120u)();
    assert(actorPhases==expected);
    assert(*Ptr<uint32_t>(0x02099f24u)==0 && *Ptr<uint32_t>(0x020a4b68u)==0);
    for (uint32_t allocation : allocated) space.release(allocation);
    std::cout << "Original actor frame: cleanup/init/behavior/render order and depth-first scene reconciliation passed.\n";
}
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
    space.bind(0x60002000u,&construct_array_element);
    space.bind(0x60002001u,&destroy_array_element);
    const auto allocationCount=space.allocations();
    arrayConstructed=0; arrayThrowAt=0; arrayDestroyed.clear();
    auto allocateArray=Ptr<uint32_t(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(0x02073470u);
    const uint32_t elements=allocateArray(7,0x60,8,0x60002000u,0x60002001u);
    assert(*Ptr<uint32_t>(elements-8)==0x60 && *Ptr<uint32_t>(elements-4)==7);
    assert(arrayConstructed==7 && arrayDestroyed.empty());
    for (unsigned i=0;i<7;++i) assert(*Ptr<uint32_t>(elements+i*0x60)==i+1);
    space.release(elements-8);
    arrayConstructed=0; arrayThrowAt=3;
    bool constructorThrew=false;
    try { (void)allocateArray(7,0x60,8,0x60002000u,0x60002001u); }
    catch (const std::runtime_error &e) { constructorThrew=std::string(e.what())=="array construction test"; }
    assert(constructorThrew && arrayDestroyed==std::vector<unsigned>({2,1}));
    assert(space.allocations()==allocationCount);
    const uint32_t wipes=allocateArray(7,0x60,8,0x02017480u,0x02017450u);
    for (unsigned i=0;i<7;++i) {
        const uint32_t wipe=wipes+i*0x60;
        assert(*Ptr<uint32_t>(wipe)==0x0208ea9cu);
        assert(*Ptr<uint32_t>(wipe+4)==0x1000 && *Ptr<uint32_t>(wipe+8)==0);
        assert(*Ptr<uint32_t>(wipe+0x10)==0x0208e90cu);
        assert(*Ptr<uint32_t>(wipe+0x14)==0 && *Ptr<uint32_t>(wipe+0x5c)==0);
        Ptr<void(uint32_t)>(0x02017450u)(wipe);
    }
    space.release(wipes-8);
    assert(space.allocations()==allocationCount);
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
void minigame_catalog_runtime() {
    using sm64ds::MinigameCatalog;
    assert(MinigameCatalog::overlay_for_scene(5) == 5);
    assert(MinigameCatalog::overlay_for_scene(0x169) == 6);
    assert(MinigameCatalog::overlay_for_scene(0x186) == 6);
    bool unsupported = false;
    try { (void)MinigameCatalog::overlay_for_scene(3); }
    catch (const std::out_of_range &) { unsupported = true; }
    assert(unsupported);
    // A synthetic table tests the parser only; production data is read from
    // overlay 5 and must pass the same checks without falling back to this.
    constexpr size_t begin = 0x20;
    std::vector<uint8_t> data(begin + MinigameCatalog::Count * 0x34 + 0x20, 0xff);
    for (uint32_t i = 0; i < MinigameCatalog::Count; ++i) {
        const size_t at = begin + size_t(i) * 0x34;
        put16(data, at, uint16_t(0x169 + (i < 30 ? i : i - 30)));
        put16(data, at + 2, 0);
        put32(data, at + 4, ((35-i) << 16) | (i << 8) | (i < 30 ? 0u : 1u));
        put32(data, at + 8, i % 6);
        put32(data, at + 12, 9999);
        put32(data, at + 16, i % 6 == 2 ? 1 : 0);
        for (uint32_t j = 0; j < 5; ++j) put32(data, at + 20 + j * 4, j);
        put32(data, at + 40, 7);
        put32(data, at + 44, 2);
        put32(data, at + 48, 0);
    }
    const auto catalog = MinigameCatalog::parse(data.data(), data.size(), 0x020ad660u);
    assert(catalog.menu_table_address() == 0x020ad680u);
    for (size_t i = 0; i < MinigameCatalog::Count; ++i) {
        assert(catalog.row(i).sceneId == (0x169 + (i < 30 ? i : i - 30)));
        assert(((catalog.row(i).sceneParameter >> 8) & 0xff) == i);
    }
    auto duplicate = data;
    put32(duplicate, begin + 35 * 0x34 + 4, 34u << 8);
    bool rejected = false;
    try { (void)MinigameCatalog::parse(duplicate.data(), duplicate.size()); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    put16(data, begin, 0x168);
    rejected = false;
    try { (void)MinigameCatalog::parse(data.data(), data.size()); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    std::cout << "Minigame overlay routing, 36-row table parser, tags and invalid-table rejection passed.\\n";
}

void minigame_art_decode() {
    using sm64ds::MinigameMenuArtwork;
    assert(MinigameMenuArtwork::language_obj_file(0) == 0xd8);
    assert(MinigameMenuArtwork::language_obj_file(1) == 0xd8);
    assert(MinigameMenuArtwork::language_obj_file(2) == 0xd5);
    assert(MinigameMenuArtwork::language_obj_file(3) == 0xd6);
    assert(MinigameMenuArtwork::language_obj_file(4) == 0xd7);
    assert(MinigameMenuArtwork::language_obj_file(5) == 0xda);
    const std::vector<uint8_t> packed{
        'L','Z','7','7',0x10,4,0,0,0,0x12,0x34,0x56,0x78};
    const auto decoded = MinigameMenuArtwork::decode_lz(packed, 4);
    assert((decoded == std::vector<uint8_t>{0x12,0x34,0x56,0x78}));
    bool rejected = false;
    try { (void)MinigameMenuArtwork::decode_lz(packed, 3); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)MinigameMenuArtwork::decode_lz(
        std::vector<uint8_t>(packed.begin(),packed.end()-1), 4); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)MinigameMenuArtwork::language_obj_file(6); }
    catch (const std::out_of_range &) { rejected = true; }
    assert(rejected);
    std::cout << "Original minigame BG/OBJ asset compression and language handle checks passed.\\n";
}

int main(int argc, char **argv) {
    if (argc>1 && std::getenv("SM64DS_MINIGAME_ONLY")) {
        minigame_gameplay_cartridge(argv[1]);
        return 0;
    }
    minigame_oam_runtime();
    minigame_state_runtime();
    minigame_base_leaf_runtime();
    minigame_constructor_runtime();
    minigame_record_runtime();
    wipe_runtime_tests();
    narc_bounds();
    minigame_catalog_runtime();
    minigame_art_decode();
    overlay_sdk();
    audio_groups();
    controller_glyph_words();
    player_state_runtime();
    item_block_state_runtime();
    texture_animation_runtime();
    moving_mesh_runtime();
    ground_runtime();
    actor_frame_runtime();
    if (argc < 2) {
        std::cout << "NARC bounds passed; native boot needs a private ROM.\n";
        return 0;
    }
    for (int repeat = 0; repeat < 4; ++repeat) {
        sm64ds::Resources resources(argv[1]);
        if (repeat==0) startup_data(argv[1]);
        sm64ds::Cartridge cartridge(argv[1]);
        if (repeat==0) minigame_state_cartridge(cartridge);
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
            const auto &menu = game.minigames();
            assert(menu.menu_table_address() >= 0x02000000u);
            for (size_t i = 0; i < sm64ds::MinigameCatalog::Count; ++i)
                assert(sm64ds::MinigameCatalog::is_game_scene(menu.row(i).sceneId));
            assert(game.boot().mFadeTimer == 60 && game.boot().mState == 0);
            assert(game.requested_scene() == 0x187);
            // Exercise the actual LoadFont(2) destination contract without
            // changing the displayed VRAM or pretending the menu is linked.
            const auto minigameFont = cartridge.file(0x980e);
            auto *fontWork = static_cast<uint8_t *>(Mg_GetWorkBuffer());
            assert(fontWork && Mg_GetWorkBuffer() == fontWork);
            assert(game.font_work_destination(fontWork, minigameFont.size()) == fontWork);
            assert(game.font_work_destination(fontWork + 1, minigameFont.size()) == nullptr);
            assert(LoadFileAt(0x980e, fontWork) == 1);
            assert(std::memcmp(fontWork, minigameFont.data(), minigameFont.size()) == 0);
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
            if (repeat==0) {
                sm64ds::MinigameProgress::unlock(24);
                const auto titleTouch=[&](int tx=-1,int ty=-1) {
                    game.tick(0,0,0,tx,ty);
                    float samples[1096]{}; music.mix(samples,548,32768);
                };
                // Real original UI: dismiss the intro, wait for the title,
                // then tap STANZA DEI GIOCHI. Do not inject return code 7.
                titleTouch(); titleTouch(128,96);
                for (unsigned i=0;i<488;++i) titleTouch();
                titleTouch(220,170);
                for (unsigned i=0;i<200 && game.active_scene()!=5;++i) titleTouch();
                for (unsigned i=0;i<40;++i) game.tick();
                assert(game.active_scene()==5 && game.minigame_runtime());
                assert(game.minigame_runtime()->group()==3); // original ARM9 initializer
                assert(game.minigame_runtime()->page()==0);
                const auto sprite_pixels=[](const ntr::Framebuffer &screen) {
                    size_t count=0;
                    for (const auto &row:screen.layer) for (const auto layer:row) count+=layer==4;
                    return count;
                };
                assert(sprite_pixels(game.main_screen())>200);
                assert(sprite_pixels(game.sub_screen())>200);
                // All 128 unused original OAM entries must be hidden after
                // Reset's intentionally overlapping forward word copies.
                const auto &menuSpace=game.minigame_runtime()->space();
                uint32_t used=0;
                std::memcpy(&used,menuSpace.data(0x0209e670,4),4);
                assert(used>0 && used<128);
                for (unsigned j=used;j<128;++j) {
                    uint32_t xy=0;
                    std::memcpy(&xy,menuSpace.data(0x0209ea74+j*8,4),4);
                    assert(xy==0xc0);
                }
                // Touching empty space must not start a scene; actual unlocked
                // icons now follow the original StartSceneFade path.
                game.tick(0,0,0,0x35,0x20); game.tick();
                assert(game.active_scene()==5 && game.requested_scene()==0x187);
                const char *capture=std::getenv("SM64DS_MENU_CAPTURE");
                if (capture) {
                    assert(ntr::ppu_write_bmp((std::string(capture)+"-main.bmp").c_str(),game.main_screen()));
                    assert(ntr::ppu_write_bmp((std::string(capture)+"-sub.bmp").c_str(),game.sub_screen()));
                }
                // Original tab table is 3,0,1,2 (not tab index == group).
                game.tick(0,0,0,0x18,0x48); game.tick();
                assert(game.minigame_runtime()->group()==0);
                game.tick(0,0,0,0x18,0x18); game.tick();
                assert(game.minigame_runtime()->group()==3);
                game.tick(0,0,0,0x18,0x48); game.tick();
                assert(game.minigame_runtime()->group()==0);
                game.tick(0,0,0,0xe0,0x60); game.tick();
                for (unsigned i=0;i<50;++i) game.tick();
                assert(game.minigame_runtime()->page()==1);
                assert(game.minigame_runtime()->scroll()==0xb0);
                game.tick(0,0,0,0x40,0x60); game.tick();
                for (unsigned i=0;i<50;++i) game.tick();
                assert(game.minigame_runtime()->page()==0 && game.minigame_runtime()->scroll()==0);
                game.tick(0,0,0,0xe8,0xa0); game.tick();
                for (unsigned i=0;i<90;++i) game.tick();
                assert(game.active_scene()==1 && game.frontend_core() && !game.minigame_runtime());
                std::cout << "Original minigame menu: source lifecycle, OAM, touchscreen tabs/pages and title round trip passed.\n";
            }
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
            bool unloadedPlayer=false;
            try { (void)stageSpace->code(0x020e6c0cu); }
            catch (const std::runtime_error &) { unloadedPlayer=true; }
            assert(unloadedPlayer);
            // SM64DS_STAGE_FRAMES=N keeps running the original castle frames
            // (no input) and reports the first actual missing dependency.
            const char *framesEnv=std::getenv("SM64DS_STAGE_FRAMES");
            const unsigned stageFrames=framesEnv?unsigned(std::strtoul(framesEnv,nullptr,0)):0;
            if (stageFrames) {
                unsigned frame=0;
                try {
                    for (;frame<stageFrames;++frame) game.tick();
                    std::cerr << "Stage frames completed: " << frame << '\n';
                } catch (const std::runtime_error &e) {
                    std::cerr << "Stage frame " << frame << " dependency: " << e.what() << '\n';
                }
                std::exit(0);
            }
            try {
                // The first castle tick now completes: spawn, original
                // InitResources, the intro KuppaScript and a full frame.
                game.tick();
                reachedStage = true;
            } catch (const std::runtime_error &e) {
                reachedStage = false;
                if (std::getenv("SM64DS_TRACE_STAGE")) {
                    std::cerr << "Actual stage dependency: " << e.what() << '\n';
                }
                if (!reachedStage) std::cerr << "Stage initialization: " << e.what() << '\n';
            }
            assert(reachedStage);
            assert(*static_cast<uint8_t *>(stageSpace->data(0x020a4b4cu, 1)) == 0); // spawn completed
            auto stageWord = [&](uint32_t address) {
                uint32_t value=0;
                std::memcpy(&value,stageSpace->data(address,4),4);
                return value;
            };
            // Inspect real spawned instances beyond the former door-factory
            // frontier. A linked constructor alone must not count as a loaded
            // actor: its original InitResources must have published live state
            // and real model/animation ownership.
            std::unordered_set<uint32_t> actorNodes;
            std::unordered_set<uint16_t> initializedTypes;
            unsigned loadedNets=0,loadedSigns=0,loadedWater=0,loadedMist=0,loadedBlocks=0,loadedMushrooms=0;
            for(uint32_t node=stageWord(0x0209b468u);node;node=stageWord(node+4)) {
                assert(actorNodes.insert(node).second && actorNodes.size()<512);
                const uint32_t instance=stageWord(node+8);
                const uint16_t id=*static_cast<uint16_t *>(stageSpace->data(instance+12,2));
                const uint32_t vtable=stageWord(instance);
                if(vtable==0x0214e47cu) {
                    ++loadedBlocks;
                    assert(stageSpace->allocation_size(instance)==0x3f8);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(stageWord(instance+0xdc) && stageWord(instance+0x328));
                    assert(stageWord(instance+0x380) && stageWord(instance+0x38c));
                    assert(stageWord(instance+0x3e8)==0 && stageWord(instance+0x3dc)==0);
                    assert(int32_t(stageWord(instance+0x9c))==-0x8000);
                    assert(stageWord(instance+0x124+0x50)==0x199);
                    assert(stageWord(instance+0x124+0x24)==0x0210d954u);
                    assert(std::memcmp(stageSpace->data(instance+0x3ac,48),stageSpace->data(instance+0xf0,48),48)==0);
                    continue;
                }
                if(vtable==0x021083c8u) {
                    ++loadedMushrooms;
                    assert(stageSpace->allocation_size(instance)==0x398);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(stageWord(instance+0x308) && stageWord(instance+0x358));
                    assert(stageWord(instance+0x140)==instance);
                    assert(stageWord(instance+0x158)==instance);
                    assert((stageWord(instance+0x154)&0x80)!=0);
                    assert(stageWord(instance+0x384)==(stageWord(instance+8)&15));
                    assert(stageWord(instance+0x390)==((stageWord(instance+8)>>4)&15));
                    for(unsigned lane=0;lane<3;++lane)
                        assert(stageWord(instance+0x378+lane*4)==stageWord(instance+0x5c+lane*4));
                    continue;
                }
                if(vtable==0x02113a18u) {
                    ++loadedWater;
                    assert(stageSpace->allocation_size(instance)==0x338);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(stageWord(instance+0xdc));
                    assert(stageWord(instance+0x330)==0x02112bc4u);
                    assert(stageWord(instance+0x324)>0);
                    assert(stageWord(instance+0x124+0x20));
                    assert(stageWord(instance+0x124+0x24)==0x02112c38u);
                    assert(stageWord(instance+0x124+0x50)==0x1000);
                    const uint8_t slot=*static_cast<uint8_t *>(stageSpace->data(instance+0x124+20,1));
                    assert(slot<24 && stageWord(0x020a0c80u+slot*4)==instance+0x124);
                    // One original actor over multiple animation wraps;
                    // this is deliberately not counted as a full world frame.
                    sm64ds::packed::Space::Scope scope(*stageSpace);
                    using sm64ds::packed::Ptr;
                    const uint32_t length=stageWord(instance+0x324)&0x3fffffffu;
                    const uint32_t materials=stageWord(instance+0xe0);
                    const uint32_t materialBytes=stageWord(stageWord(instance+0xdc)+0x24)*0x30;
                    assert(materialBytes>0 && materialBytes<65536);
                    std::vector<uint8_t> before(materialBytes);
                    std::memcpy(before.data(),stageSpace->data(materials,materialBytes),materialBytes);
                    bool materialsChanged=false;
                    for(unsigned frame=0;frame<400;++frame) {
                        assert(Ptr<int(uint32_t)>(0x02111c4cu)(instance)==1);
                        ntr::gx_begin_frame();
                        assert(Ptr<int(uint32_t)>(0x02111c18u)(instance)==1);
                        size_t polygons=0;
                        (void)ntr::gx_polygons(polygons);
                        assert(polygons>0);
                        assert(stageWord(instance+0x328)==((frame+1)*0x1000u)%length);
                        materialsChanged|=std::memcmp(before.data(),stageSpace->data(materials,materialBytes),materialBytes)!=0;
                    }
                    assert(materialsChanged);
                    continue;
                }
                if(vtable==0x021094a0u) {
                    ++loadedMist;
                    assert(stageSpace->allocation_size(instance)==0xdc);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(*static_cast<uint16_t *>(stageSpace->data(instance+0xd4,2))==0x24);
                    assert(stageWord(instance+0xd8)==0);
                    bool originalPosition=false;
                    for(unsigned i=0;i<7;++i)
                        originalPosition|=std::memcmp(stageSpace->data(instance+0x5c,12),
                                                      stageSpace->data(0x02113d8cu+i*12,12),12)==0;
                    assert(originalPosition);
                    continue;
                }
                if(vtable==0x02109af8u) {
                    ++loadedSigns;
                    assert(stageSpace->allocation_size(instance)==0x5a4);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(stageWord(instance+0xdc) && stageWord(instance+0x360));
                    assert(stageWord(instance+0x124+0x20));
                    assert(stageWord(instance+0x124+0x24)==0x0210d714u);
                    assert(stageWord(instance+0x124+0x50)==0x199);
                    assert(stageWord(instance+0x350)==instance);
                    assert(stageWord(instance+0x3c8+0x14)==instance);
                    assert(stageWord(instance+0x3c8+0x18)==0x28000);
                    assert((*static_cast<uint8_t *>(stageSpace->data(instance+0x3c8+0x24,1))&2)!=0);
                    assert((*static_cast<uint8_t *>(stageSpace->data(instance+0x3c8+0x138,1))&2)!=0);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+0x58e,1))==2);
                    for(unsigned lane=0;lane<3;++lane) {
                        assert(stageWord(instance+0x3b0+lane*4)==stageWord(instance+0x5c+lane*4));
                        assert(int32_t(stageWord(instance+0x3a4+lane*4))==
                               (int32_t(stageWord(instance+0x5c+lane*4))>>3));
                    }
                    assert(int32_t(stageWord(instance+0x9c))==-0x2000);
                    continue;
                }
                if(vtable==0x02113ae0u || vtable==0x02130400u) {
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+14,1))==1);
                    assert(stageWord(instance+0xdc));
                    assert(stageWord(instance+0x124)==0x02099434u);
                    if(vtable==0x02113ae0u) {
                        ++loadedNets;
                        assert(stageSpace->allocation_size(instance)==0x320);
                        const uint32_t mesh=instance+0x124,kcl=stageWord(mesh+0x20);
                        assert(kcl && stageSpace->allocation_size(kcl)>=0x38);
                        assert(stageWord(mesh+0x24)==0x02112bf8u);
                        assert(stageWord(mesh+0x50)==0x1000 && stageWord(mesh+0x164)==0x1000);
                        for(unsigned lane=0;lane<3;++lane)
                            assert(stageWord(mesh+0x118+lane*4)==stageWord(instance+0x5c+lane*4));
                    } else {
                        assert(stageSpace->allocation_size(instance)==0x368);
                        assert(stageWord(instance+0x320)==0x0208e704u);
                        assert(stageWord(instance+0x350)==instance);
                    }
                    continue;
                }
                if(id!=353 && id!=343 && id!=336 && id!=207 && id!=286) continue;
                const uint8_t life=*static_cast<uint8_t *>(stageSpace->data(instance+14,1));
                if(id==286 && life!=1) {
                    // Original tree batching keeps one live actor per model
                    // variant; the additional colliders live in TreeNodes.
                    // After a whole frame the scheduler has queued them for
                    // destruction (aliveState 2), as the original reconcile does.
                    assert((life==0 && *static_cast<uint8_t *>(stageSpace->data(instance+15,1))==1) ||
                           life==2);
                    continue;
                }
                initializedTypes.insert(id);
                assert(life==1);
                if(id==286) {
                    bool modelLoaded=false;
                    for(unsigned i=0;i<5;++i) modelLoaded|=stageWord(instance+0xd8+i*0x50)!=0;
                    assert(modelLoaded);
                } else {
                    assert(stageWord(instance+0xdc)); // shared model data; no owned modelFile
                    if(id!=207) assert(stageWord(instance+0x134)); // original BCA
                }
                if(id==353) assert(stageWord(instance)==0x02148188u && stageWord(instance+0x140));
                if(id==343) assert(stageWord(instance+0x138)==0x0208e868u && stageWord(instance+0x140));
                if(id==336) {
                    assert(stageWord(instance+0x140) && stageWord(instance+0x190));
                    assert(stageWord(instance+0x394+0x30)==instance);
                }
                if(id==207) {
                    assert(stageWord(instance+0x124)==0x0208e6d4u);
                    assert(stageWord(instance+0x124+0x30)==instance);
                    assert(*static_cast<uint8_t *>(stageSpace->data(instance+0x184,1))==2);
                }
            }
            for(uint16_t id : {353,343,336,207,286}) assert(initializedTypes.count(id));
            assert(loadedNets>0);
            assert(loadedSigns>0);
            assert(loadedBlocks>0 && loadedMushrooms>0);
            assert(loadedWater>0 && loadedMist==loadedWater*7);
            // All eleven actual modules and all 58 static initializers ran.
            // Code stays the real BLZ result. Constructors build handles and
            // state tables in writable data/BSS instead of fabricated data.
            const unsigned modules[]={1,2,9,62,69,85,89,94,100,98,102};
            for(unsigned i=0;i<11;++i) {
                ntr::OverlayInfo info{};
                assert(ntr::fs_overlay_info(modules[i],info));
                const auto expectedImage=cartridge.overlay_data(modules[i]);
                bool checkedCode=false;
                for(const auto &range:stageCodeRanges) if(range.module==modules[i]) {
                    if(!range.start && !range.end) { checkedCode=true; continue; }
                    assert(range.start>=info.ramAddress && range.end>range.start);
                    assert(range.end-info.ramAddress<=info.ramSize);
                    const uint32_t offset=range.start-info.ramAddress,bytes=range.end-range.start;
                    assert(std::memcmp(stageSpace->data(range.start,bytes),
                                       expectedImage.bytes.data()+offset,bytes)==0);
                    checkedCode=true;
                }
                assert(checkedCode);
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
            // The sound-bank bytes (0x0209b47c/0x0208e428) are no longer the
            // InitResources values here: the intro KuppaScript started on the
            // completed first frame loads its own group (StartIntroCutscene).
            for(auto handle : {std::pair<uint32_t,uint32_t>{0x0210da18u,0x3c7},
                               {0x02113c20u,0x438},{0x02113c28u,0x439},
                               {0x02113c68u,0x693},{0x02113c70u,0x694},
                               {0x02148934u,0x4a1}}) {
                assert(*static_cast<uint16_t *>(stageSpace->data(handle.first,2))==
                       resources.file_id(handle.second));
                if (handle.first==0x02113c20u || handle.first==0x02113c28u ||
                    handle.first==0x02113c68u || handle.first==0x02113c70u) {
                    assert(*static_cast<uint8_t *>(stageSpace->data(handle.first+2,1))>=1);
                    assert(stageSpace->allocation_size(stageWord(handle.first+4))>0);
                } else {
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
            assert(stageWord(0x0209cef8u)>=20);
            for(unsigned i=0;i<12;++i) {
                const uint32_t holder=stageWord(0x020756f0u+i*4),file=stageWord(holder+4);
                assert(*static_cast<uint8_t *>(stageSpace->data(holder+2,1))>=1);
                assert(file && stageSpace->allocation_size(file)>=0x40);
                assert(stageWord(file+8)>=file); // original BMD offsets became checked pointers
                assert(stageWord(0x0209cefc+i*12)==file);
            }
            const uint32_t wipes=stageWord(0x0209f324u);
            assert(wipes && stageWord(wipes-8)==0x60 && stageWord(wipes-4)==7);
            for (unsigned i=0;i<7;++i) {
                const uint32_t wipe=wipes+i*0x60,model=wipe+0x10;
                assert(stageWord(wipe)==0x0208ea9cu && stageWord(wipe+4)==0x1000);
                assert(stageWord(model)==0x0208e90cu && stageWord(model+4));
                assert(stageWord(model+8)==stageWord(model+4));
                assert(stageWord(model+0x4c) && stageWord(model+0x14));
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
            assert(*static_cast<uint8_t *>(stageSpace->data(actor+0x91c+20,1))==0);
            assert(stageWord(0x020a0c80u)==actor+0x91c);
            const uint32_t kcl=stageWord(actor+0x91c+0x20);
            assert(kcl && stageSpace->allocation_size(kcl)>=assets->collision().size());
            for (unsigned offset : {0u,4u,8u,12u}) {
                uint32_t original=0;
                std::memcpy(&original,assets->collision().data()+offset,4);
                assert(stageWord(kcl+offset)==kcl+original);
            }
            assert(std::memcmp(stageSpace->data(kcl+16,assets->collision().size()-16),
                               assets->collision().data()+16,assets->collision().size()-16)==0);
            assert(stageWord(actor+0x9c4)==0);
            // The real entrance loader constructed and initialized the Player
            // through its full entrance-state initializer. Door's overlay-100
            // factory is now the next original dependency reached naturally.
            const uint32_t playerNode=stageWord(0x0209b468u),player=stageWord(playerNode+8);
            assert(playerNode==player+0x50 && stageSpace->allocation_size(player)>=0x768);
            assert(stageWord(player)==0x0210a83cu);
            assert(*static_cast<uint16_t *>(stageSpace->data(player+12,2))==0xbf);
            assert(*static_cast<uint8_t *>(stageSpace->data(player+0x6d8,1))==0);
            assert(*static_cast<uint8_t *>(stageSpace->data(player+0x6d9,1))==request.character);
            for (auto position : {std::pair<unsigned,int32_t>{0x5c,firstEntrance.x},
                                   {0x60,firstEntrance.y},{0x64,firstEntrance.z}})
                assert(stageWord(player+position.first)==static_cast<uint32_t>(position.second));
            assert(stageWord(player+0x370)==0x021104b4u && stageWord(player+0x378)==0x021104b4u);
            const uint32_t trail=stageWord(player+0x578);
            assert(trail && stageWord(trail-8)==12 && stageWord(trail-4)==50);
            assert(stageSpace->allocation_size(stageWord(player+0x57c))>=50);
            const uint32_t heldQueue=stageWord(player+0x588);
            assert(stageSpace->allocation_size(heldQueue)>=20);
            for (unsigned i=0;i<20;++i)
                assert(*static_cast<uint8_t *>(stageSpace->data(heldQueue+i,1))==0);
            for (unsigned i=0;i<5;++i) {
                const uint32_t model=stageWord(player+0xdc+i*4);
                assert(model && stageSpace->allocation_size(model)>=0x78);
                assert(stageWord(model)==0x0208e9b4u && stageWord(model+8));
            }
            for (unsigned i=0;i<8;++i) {
                const uint32_t model=stageWord(player+0x154+i*4);
                assert(model && stageWord(model)==(i==3?0x0208e980u:0x0208e90cu));
                assert(stageWord(model+8));
            }
            {
                // Execute the Player's own animation selection/advancement,
                // then the animated body's original bone and draw pipeline.
                // This does not replace Player::Behavior/Render or move it.
                sm64ds::packed::Space::Scope scope(*stageSpace);
                using sm64ds::packed::Ptr;
                const uint32_t bodyID=Ptr<uint32_t(uint32_t,uint32_t,int)>(0x020becc8u)(
                    player,stageWord(player+8)&0xffu,0);
                assert(bodyID<5);
                const uint32_t body=stageWord(player+0xdc+bodyID*4);
                const uint32_t bones=stageWord(stageWord(body+8)+4),matrices=stageWord(body+0x14);
                assert(bones>0 && bones<256);
                std::vector<uint8_t> initial(bones*48);
                std::memcpy(initial.data(),stageSpace->data(matrices,initial.size()),initial.size());
                bool poseChanged=false,cursorChanged=false;
                for(unsigned frame=0;frame<180;++frame) {
                    const uint32_t flags=stageWord(body+0x54),length=flags&0x3fffffffu;
                    const int32_t before=int32_t(stageWord(body+0x58));
                    assert(length>0);
                    int64_t expected=int64_t(before)+int32_t(stageWord(body+0x5c));
                    if(flags&0xc0000000u)
                        expected=std::clamp<int64_t>(expected,0,length-1);
                    else expected=(expected+length)%length;
                    Ptr<void(uint32_t)>(0x020bedd4u)(player);
                    assert(int32_t(stageWord(body+0x58))==expected);
                    cursorChanged|=before!=expected;
                    ntr::gx_begin_frame();
                    Ptr<void(uint32_t,uint32_t)>(0x020167f8u)(body,0);
                    size_t polygons=0;
                    (void)ntr::gx_polygons(polygons);
                    assert(polygons>0);
                    poseChanged|=std::memcmp(initial.data(),stageSpace->data(matrices,initial.size()),initial.size())!=0;
                }
                assert(cursorChanged && poseChanged);
                std::cout << "Original Player body " << bodyID
                          << ": 180 animation/bone/render updates passed.\n";
            }
            assert(stageWord(player+0x2ac)==0x0208e868u && stageWord(player+0x2ac+8));
            assert(stageWord(player+0x2d4+0x30)==player);
            assert(stageWord(player+0x2d4+4)==0x28000 && stageWord(player+0x2d4+8)==0x96000);
            assert(stageWord(player+0x380+0x14)==player && stageWord(player+0x380+0x18)==0x32000);
            assert(stageWord(player+0x380+0x10c)==0x28000);
            assert(stageWord(player+0xa0)==uint32_t(-0x4b000));
            assert(*static_cast<uint8_t *>(stageSpace->data(player+14,1))==1);
            assert(*static_cast<uint8_t *>(stageSpace->data(player+0x6e5,1))==2);
            assert(*static_cast<uint8_t *>(stageSpace->data(player+0x709,1))==1);
            const uint32_t camera=stageWord(0x0209f318u);
            assert(camera && stageSpace->allocation_size(camera)==0x1a8);
            assert(stageWord(camera)==0x02086f84u && stageWord(camera+0x110)==player);
            assert(*static_cast<uint8_t *>(stageSpace->data(camera+14,1))==1);
            assert(stageWord(camera+0xf8)==0x1555 && stageWord(camera+0xfc)==0x1000);
            assert(stageWord(camera+0x100)==0x1388000);
            assert(std::memcmp(stageSpace->data(camera+0x50,0x30),
                               stageSpace->data(0x0209b3ecu,0x30),0x30)==0);
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

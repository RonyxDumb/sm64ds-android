#include "stage_runtime.h"
#include "actor_dispatch.h"
#include "collision_dispatch.h"
#include "boot_seams.h"
#include "packed_ptr.h"
#include "platform/arm9_services.h"
#include "ntr/mmio.h"
#include "game_program.h"
#include <cstring>
#include <exception>
#include <unordered_set>
#include <stdexcept>

extern "C" uint8_t GetControllerMode(int);
extern "C" uint8_t NumStars();
extern "C" int GetOwnerLanguage();
extern "C" void LoadFont(uint8_t);
extern "C" int LoadFileAt(int,void *);
extern "C" uint8_t data_0209d698;
extern "C" uint8_t data_0209d660;
extern "C" short data_0209d6d4;
extern "C" void pk_MultiStore16(unsigned short,sm64ds::packed::Ptr<char>,int);
extern "C" void pk_MultiStore_Int(int,sm64ds::packed::Ptr<int>,int);
extern "C" void pk_MultiCopy_Int(sm64ds::packed::Ptr<int>,sm64ds::packed::Ptr<int>,int);
extern "C" int pk__ZN4cstd4fdivEii(int,int);
extern "C" void pk_func_02052820(sm64ds::packed::Ptr<int>,int,int);
extern "C" void pk_func_02052800(sm64ds::packed::Ptr<int>,int,int);
extern "C" void pk_func_0205283c(sm64ds::packed::Ptr<int>,int,int);
extern "C" void pk_mg_DecompressLZ16(sm64ds::packed::Ptr<void>,sm64ds::packed::Ptr<void>);
namespace cstd { int sqrt(unsigned long long); int16_t atan2(int32_t,int32_t); }

namespace sm64ds {
namespace {
using packed::Ptr;
template<class T> T &at(uint32_t address) { return *Ptr<T>(address); }

// func_0203b438, ActorBase_SceneNode.cpp. Nodes retain their ROM owner
// back-pointers and four separate tree links.
bool append_scene(uint32_t root, uint32_t node, uint32_t parent) {
    if (!node) return false;
    if (!parent) {
        if (at<uint32_t>(root)) return false;
        at<uint32_t>(root)=node;
        return true;
    }
    at<uint32_t>(node)=parent;
    uint32_t tail=at<uint32_t>(parent+4);
    if (!tail) at<uint32_t>(parent+4)=node;
    else {
        std::unordered_set<uint32_t> visited;
        while (at<uint32_t>(tail+12)) {
            if (!visited.insert(tail).second)
                throw std::runtime_error("Cyclic actor scene tree.");
            tail=at<uint32_t>(tail+12);
        }
        at<uint32_t>(tail+12)=node;
        at<uint32_t>(node+8)=tail;
    }
    return true;
}

// func_0203b244: append the existing 0x10-byte actor-owned process node.
void append_process(uint32_t list, uint32_t node) {
    if (!node) return;
    const uint32_t tail=at<uint32_t>(list+4);
    if (tail) {
        at<uint32_t>(tail+4)=node;
        at<uint32_t>(node)=tail;
    } else at<uint32_t>(list)=node;
    at<uint32_t>(list+4)=node;
}
void unlink_process(uint32_t list,uint32_t node) {
    if (!node) return;
    const uint32_t previous=at<uint32_t>(node),next=at<uint32_t>(node+4);
    if (previous) at<uint32_t>(previous+4)=next;
    else if (at<uint32_t>(list)==node) at<uint32_t>(list)=next;
    if (next) at<uint32_t>(next)=previous;
    else if (at<uint32_t>(list+4)==node) at<uint32_t>(list+4)=previous;
    at<uint32_t>(node)=at<uint32_t>(node+4)=0;
}
// func_0204405c: stable priority order, including equal-priority actors.
void insert_process(uint32_t list,uint32_t node) {
    if (!node) return;
    uint32_t previous=0,next=at<uint32_t>(list);
    std::unordered_set<uint32_t> visited;
    while (next && at<uint16_t>(next+12)<=at<uint16_t>(node+12)) {
        if (!visited.insert(next).second) throw std::runtime_error("Cyclic actor priority list.");
        previous=next;next=at<uint32_t>(next+4);
    }
    at<uint32_t>(node)=previous;at<uint32_t>(node+4)=next;
    if (previous) at<uint32_t>(previous+4)=node;
    else at<uint32_t>(list)=node;
    if (next) at<uint32_t>(next)=node;
    else at<uint32_t>(list+4)=node;
}

bool stage_before_init(uint32_t actor) {
    // The complete dScStage_c::BeforeInitResources body. The real scene
    // method must be bound separately; an unknown method never succeeds.
    return Ptr<bool(uint32_t)>(0x0202e66cu)(actor);
}

bool fbase_before_init(uint32_t actor) {
    (void)packed::Space::current().data(actor,0x50);
    return true;
}

bool scene_reset_faders_and_sound(uint32_t actor) {
    // Complete dScene_c::ResetFadersAndSound. The shared color fader is
    // native-owned by GameProgram, so its actual C++ methods use the host
    // object. The current scene remains a checked cartridge actor handle.
    at<uint32_t>(0x0209f5c0u)=actor;
    if (!fbase_before_init(actor)) return false;
    dScene_c::SetFaders(&data_0209f5e8);
    at<uint32_t>(0x0209f1e4u)=0;
    Ptr<void()>(0x02011b7cu)();
    return true;
}

void stage_flush_fifo(uint32_t port) {
    // The original hand-unrolled func_020553c0 writes 32 groups of four
    // zero words to a fixed destination. Reuse its verified native primitive.
    void *destination;
    if (port>=ntr::IO_BASE && uint64_t(port)+16<=ntr::IO_BASE+ntr::IO_SIZE)
        destination=reinterpret_cast<void *>(static_cast<uintptr_t>(port));
    else destination=packed::Space::current().data(port,16);
    const uint32_t zeros[128]{};
    platform::copy_fixed(zeros,destination,sizeof zeros,16);
}

void stage_fill(uint32_t destination,int value,int bytes) {
    if (bytes<0) throw std::runtime_error("Negative stage memory fill extent.");
    if (bytes) std::memset(packed::Space::current().data(destination,static_cast<uint32_t>(bytes)),value,static_cast<uint32_t>(bytes));
}
int stage_load_file_at(int handle,uint32_t destination) {
    return LoadFileAt(handle,reinterpret_cast<void *>(static_cast<uintptr_t>(destination)));
}
uint32_t stage_load_compressed_at(uint16_t handle,uint32_t destination) {
    return LoadCompressedFileAt(handle,reinterpret_cast<void *>(static_cast<uintptr_t>(destination)));
}
void stage_load_font(uint8_t kind) {
    LoadFont(kind);
    at<uint8_t>(0x0209d698u)=data_0209d698;
    at<uint8_t>(0x0209d660u)=data_0209d660;
    at<int16_t>(0x0209d6d4u)=data_0209d6d4;
}
}

uint32_t native_fbase_construct(uint32_t actor) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0x50);
    at<uint32_t>(actor)=0x02099edcu;
    // include/fBase_c.h::Manager and SceneNode constructors.
    for (unsigned off : {0x14u,0x18u,0x1cu,0x20u}) at<uint32_t>(actor+off)=0;
    at<uint32_t>(actor+0x24)=actor;
    for (unsigned off : {0x28u,0x38u}) {
        at<uint32_t>(actor+off)=0;
        at<uint32_t>(actor+off+4)=0;
        at<uint32_t>(actor+off+8)=actor;
        at<uint16_t>(actor+off+12)=0;
        at<uint16_t>(actor+off+14)=0;
    }
    at<uint32_t>(actor+4)=at<uint32_t>(0x02099e70u)++;
    at<uint32_t>(actor+8)=at<uint32_t>(0x020a4b60u);
    const uint16_t id=at<uint16_t>(0x020a4b54u);
    at<uint16_t>(actor+12)=id;
    at<uint8_t>(actor+0x12)=at<uint8_t>(0x020a4b48u);
    const uint32_t parent=at<uint32_t>(0x020a4b64u);
    (void)append_scene(0x020a4b6cu,actor+0x14,parent);
    const uint32_t table=at<uint32_t>(0x020a4bb8u);
    const uint32_t info=at<uint32_t>(table+uint32_t(id)*4);
    at<uint16_t>(actor+0x34)=at<uint16_t>(actor+0x36)=at<uint16_t>(info+4);
    at<uint16_t>(actor+0x44)=at<uint16_t>(actor+0x46)=at<uint16_t>(info+6);
    const uint32_t parentActor=parent ? at<uint32_t>(parent+0x10) : 0;
    if (parentActor) {
        const auto flags=at<uint8_t>(parentActor+0x13);
        if (flags&3) at<uint8_t>(actor+0x13)|=2;
        if (flags&12) at<uint8_t>(actor+0x13)|=8;
    }
    return actor;
}

uint32_t native_particle_tracker_construct(uint32_t tracker) {
    (void)packed::Space::current().data(tracker,0x81c);
    // Particle_SysTracker_Contents.cpp::Contents(). Preserve the bytes that
    // its constructor does not write; fBase::operator new clears the arena.
    for (unsigned i=0;i<64;++i) at<uint32_t>(tracker+0x10+i*0x1c)=0;
    at<uint32_t>(tracker+8)=0;
    at<uint8_t>(tracker+12)=0;
    for (unsigned i=0;i<16;++i) at<uint32_t>(tracker+0x710+i*4)=0;
    // dPa_c callback constructors, followed by their implicit derived vptr
    // stores. These are the callable slot arrays in the EU symbol map.
    for (unsigned off : {0x754u,0x760u,0x79cu,0x7a8u}) {
        at<uint32_t>(tracker+off)=0x0208f3c4u;
        at<uint16_t>(tracker+off+4)=0;
    }
    for (auto cb : {std::pair<unsigned,uint32_t>{0x76c,0x0208f3e4u},
                    {0x778,0x0208f3e4u},{0x784,0x0208f3a4u},
                    {0x790,0x0208f444u},{0x808,0x0208f404u}}) {
        at<uint32_t>(tracker+cb.first)=cb.second;
        at<uint16_t>(tracker+cb.first+4)=0;
    }
    for (unsigned off : {0x7b4u,0x7c4u,0x7d4u,0x7e4u}) {
        at<uint32_t>(tracker+off)=0x0208f414u;
        at<uint16_t>(tracker+off+4)=0;
        at<uint16_t>(tracker+off+6)=0x1000;
        at<uint16_t>(tracker+off+8)=0;
    }
    at<uint32_t>(tracker+0x7f0)=0x0208f3f4u;
    at<uint32_t>(tracker+0x7f4)=0x0208f424u;
    for (unsigned off : {0x7f8u,0x800u}) {
        at<uint32_t>(tracker+off)=0x0208f454u;
        at<uint32_t>(tracker+off+4)=0x3000;
    }
    at<uint32_t>(tracker+0x810)=0x0208f434u;
    at<uint8_t>(tracker+0x814)=1;
    at<uint32_t>(tracker+0x818)=0x0208f464u;
    // Particle.cpp::SysTracker() body.
    at<uint32_t>(0x0209ee74u)=tracker;
    at<uint32_t>(tracker+4)=0;
    for (unsigned off : {0x750u,0x75cu,0x768u,0x774u,0x780u,0x78cu,
                         0x798u,0x7a4u,0x7b0u,0x7c0u}) at<uint32_t>(tracker+off)=0;
    at<uint32_t>(tracker+0x804)=0x4b000;
    return tracker;
}

uint32_t native_simple_model_construct(uint32_t model) {
    auto &space=packed::Space::current();
    (void)space.data(model,0x50);
    // dExtModel_c(), then dExtSimpleModel_c(). Components have no ctor.
    at<uint32_t>(model)=0x0208e90cu;
    at<uint32_t>(model+4)=0;
    at<uint32_t>(model+0x4c)=0;
    std::memcpy(space.data(model+0x1c,48),space.data(0x02082128u,48),48);
    return model;
}

namespace {
uint32_t stage_allocate(uint32_t bytes) { return packed::Space::current().allocate(bytes); }
void stage_deallocate(uint32_t address) { if (address) packed::Space::current().release(address); }
// MSL func_02073470: preserve the cartridge's eight-byte cookie and use
// registered native callbacks for element construction and exception cleanup.
uint32_t stage_array_new(uint32_t count,uint32_t stride,uint32_t cookie,
                         uint32_t constructor,uint32_t destructor) {
    auto &space=packed::Space::current();
    const uint64_t bytes=uint64_t(count)*stride+cookie;
    if (!stride || bytes>UINT32_MAX || (cookie && cookie<4))
        throw std::runtime_error("Invalid cartridge array allocation extent.");
    const uint32_t block=space.allocate(static_cast<uint32_t>(bytes));
    const uint32_t base=block+cookie;
    if (cookie) {
        at<uint32_t>(base-4)=count;
        if (cookie>=8) at<uint32_t>(base-8)=stride;
    }
    uint32_t built=0;
    try {
        if (constructor)
            for (;built<count;++built) Ptr<void(uint32_t)>{constructor}(base+built*stride);
    } catch (...) {
        try {
            if (destructor)
                while (built) Ptr<void(uint32_t)>{destructor}(base+(--built)*stride);
        } catch (...) { std::terminate(); } // original func_02073534 termination path
        space.release(block);
        throw;
    }
    return base;
}
uint32_t stage_wipe_construct(uint32_t wipe) {
    (void)packed::Space::current().data(wipe,0x60);
    // dFdWipe_c's implicit inline base constructors, then its model member.
    at<uint32_t>(wipe)=0x0208ea9cu;
    at<int32_t>(wipe+4)=0x1000;
    at<int32_t>(wipe+8)=0;
    at<uint16_t>(wipe+12)=0;
    native_simple_model_construct(wipe+0x10);
    return wipe;
}
void stage_wipe_destroy(uint32_t wipe) {
    auto &space=packed::Space::current();
    (void)space.data(wipe,0x60);
    at<uint32_t>(wipe)=0x0208ea9cu;
    const uint32_t model=wipe+0x10;
    at<uint32_t>(model)=0x0208e90cu;
    if (const uint32_t buffer=at<uint32_t>(model+0x4c)) space.release(buffer);
    at<uint32_t>(model)=0x0208e87cu;
    if (const uint32_t file=at<uint32_t>(model+4)) space.release(file);
    at<uint32_t>(wipe)=0x0208eb2cu;
    at<uint32_t>(wipe)=0x0208eaccu;
    at<uint32_t>(wipe)=0x0208eafcu;
}
void stage_wipe_load(uint32_t wipe,uint16_t file) {
    // Complete dFdWipe_c::LoadAndSetFile body, including original model load.
    Ptr<void(uint32_t,uint16_t,int,int)>(0x02016f9cu)(wipe+0x10,file,0,-1);
}
void stage_path_nodes(uint32_t nodes) { at<uint32_t>(0x020a0d88u)=nodes; }
void stage_path_defs(uint32_t definitions,int count) {
    at<uint32_t>(0x020a0d84u)=definitions;
    at<int32_t>(0x020a0d8cu)=count;
}
// Complete PathPtr leaves (src/engine/path/PathPtr.cpp). PathDef records
// are six bytes; node rows are three s16 values scaled by 1 << 12.
uint32_t stage_path_construct(uint32_t path) {
    at<uint32_t>(path)=0; at<uint32_t>(path+4)=0; return path;
}
void stage_path_set_def(uint32_t path,uint32_t def) { at<uint32_t>(path)=def; }
void stage_path_from_id(uint32_t path,uint32_t id) {
    stage_path_set_def(path,at<uint32_t>(0x020a0d84u)+id*6u);
}
uint32_t stage_path_node_table() { return at<uint32_t>(0x020a0d88u); }
uint32_t stage_path_num_nodes(uint32_t path) { return at<uint8_t>(at<uint32_t>(path)+2); }
uint32_t stage_path_unk004(uint32_t path) { return at<uint8_t>(at<uint32_t>(path)+4); }
uint32_t stage_path_pull_factor(uint32_t path) { return at<uint8_t>(at<uint32_t>(path)+5)&0x7fu; }
uint32_t stage_path_loops(uint32_t path) { return at<uint8_t>(at<uint32_t>(path)+5)&0x80u; }
void stage_path_get_node(uint32_t path,uint32_t node,uint32_t index) {
    const uint32_t row=stage_path_node_table()+uint32_t(at<uint16_t>(at<uint32_t>(path)))*6u+index*6u;
    for (unsigned i=0;i<3;++i)
        at<int32_t>(node+i*4)=int32_t(uint32_t(int32_t(at<int16_t>(row+i*2)))<<12);
}
void stage_kcl_rebase(uint32_t file) {
    auto &space=packed::Space::current();
    (void)space.data(file,0x38);
    for (unsigned offset : {0u,4u,8u,12u}) {
        const uint64_t target=uint64_t(file)+at<uint32_t>(file+offset);
        if (target>UINT32_MAX) throw std::runtime_error("KCL pointer relocation overflow.");
        (void)space.data(static_cast<uint32_t>(target),1);
        at<uint32_t>(file+offset)=static_cast<uint32_t>(target);
    }
}
void stage_kcl_set_file(uint32_t collider,uint32_t file,uint32_t clps) {
    auto &space=packed::Space::current();
    (void)space.data(collider,0x50);
    (void)space.data(file,0x38);
    (void)space.data(clps,8);
    // func_02039624 and dBgW_Kc::SetFile. Preserve the derived vtable.
    at<uint32_t>(collider+4)=0;
    at<int32_t>(collider+8)=-1;
    at<int32_t>(collider+12)=-0x1000;
    at<int32_t>(collider+16)=0;
    at<uint8_t>(collider+20)=0x18;
    at<uint32_t>(collider+24)=at<uint32_t>(collider+28)=0;
    at<uint32_t>(collider+0x20)=file;
    at<uint32_t>(collider+0x24)=clps;
    at<int32_t>(collider+0x28)=at<int32_t>(collider+0x30)=0;
    at<int32_t>(collider+0x2c)=0x1000;
    at<uint8_t>(collider+0x34)=at<uint8_t>(collider+0x35)=0;
    at<int32_t>(collider+0x38)=0x1000;
    at<int32_t>(collider+0x3c)=at<int32_t>(collider+0x40)=0;
    at<int32_t>(collider+0x44)=-0x1000;
    at<int32_t>(collider+0x48)=2;
    at<uint8_t>(collider+0x4c)=1;
    at<uint8_t>(collider+0x4d)=0;
}
int stage_collider_enable(uint32_t collider,uint32_t actor) {
    (void)packed::Space::current().data(collider,0x20);
    for (unsigned slot=0;slot<24;++slot) {
        const uint32_t cell=0x020a0c80u+slot*4;
        if (!at<uint32_t>(cell)) {
            at<uint32_t>(collider+4)=actor;
            at<uint32_t>(collider+8)=actor ? at<uint32_t>(actor+4) : UINT32_MAX;
            at<uint8_t>(collider+20)=static_cast<uint8_t>(slot);
            at<uint32_t>(cell)=collider;
            return 1;
        }
    }
    return 0;
}
int32_t stage_kcl_origin_y(uint32_t collider) {
    return static_cast<int32_t>(at<uint32_t>(at<uint32_t>(collider+0x20)+0x18)<<6);
}
int32_t stage_kcl_far_y(uint32_t collider) {
    const uint32_t file=at<uint32_t>(collider+0x20);
    return static_cast<int32_t>(static_cast<uint32_t>(stage_kcl_origin_y(collider))+
                                (~at<uint32_t>(file+0x24)<<12));
}
void stage_vertical_bounds(int32_t near,int32_t far) {
    at<int32_t>(0x02092138u)=static_cast<int32_t>(static_cast<uint32_t>(near)-0x64000u);
    at<int32_t>(0x0209212cu)=static_cast<int32_t>(static_cast<uint32_t>(far)+0x64000u);
}
uint32_t stage_spawn(uint32_t id,uint32_t parent,uint32_t parameter,uint32_t kind) {
    if (!parent) return 0; // func_02042ffc
    auto &space=packed::Space::current();
    at<uint16_t>(0x020a4b50u)=static_cast<uint16_t>(id);
    at<uint8_t>(0x020a4b4cu)=2;
    const uint32_t hook=at<uint32_t>(0x020a4b58u);
    if (hook && Ptr<int(uint32_t)>{hook}(id)==3) return 0;
    at<uint8_t>(0x020a4b4cu)=3;
    at<uint32_t>(0x020a4b60u)=parameter;
    at<uint16_t>(0x020a4b54u)=static_cast<uint16_t>(id);
    at<uint8_t>(0x020a4b48u)=static_cast<uint8_t>(kind);
    at<uint32_t>(0x020a4b64u)=parent+0x14;
    at<uint8_t>(0x020a4b4cu)=4;
    const uint64_t slot=uint64_t(at<uint32_t>(0x020a4bb8u))+uint64_t(id)*4;
    if (slot>UINT32_MAX) throw std::runtime_error("Actor profile index overflow.");
    const uint32_t profile=at<uint32_t>(static_cast<uint32_t>(slot));
    const uint32_t factory=at<uint32_t>(profile);
    const uint32_t actor=Ptr<uint32_t()>{factory}();
    if (actor) {
        (void)space.data(actor,0x50);
        at<uint8_t>(0x020a4b4cu)=5;
        native_actor_post_construct(actor);
    }
    at<uint8_t>(0x020a4b4cu)=0;
    at<uint16_t>(0x020a4b50u)=0xffff;
    return actor;
}
uint32_t stage_spawn_actor(uint32_t id,uint32_t parameter,uint32_t position,
                            uint32_t rotation,int area,int deathTable) {
    auto &space=packed::Space::current();
    if (position) (void)space.data(position,12);
    if (rotation) (void)space.data(rotation,6);
    // Complete func_02010e78 followed by the original dBase_c::Spawn call.
    at<uint32_t>(0x0209b460u)=position;
    at<uint32_t>(0x0209b45cu)=rotation;
    at<int8_t>(0x0209b44cu)=static_cast<int8_t>(area);
    at<int16_t>(0x0208e378u)=static_cast<int16_t>(deathTable);
    at<uint32_t>(0x0209b458u)=0;
    return stage_spawn(id,at<uint32_t>(0x0209f5c0u),parameter,2);
}
uint32_t stage_actor_construct(uint32_t actor) {
    auto &space=packed::Space::current();
    (void)space.data(actor,0xd0);
    native_fbase_construct(actor);
    at<uint32_t>(actor)=0x0208e3a4u;
    at<uint32_t>(actor+0x50)=at<uint32_t>(actor+0x54)=0;
    at<uint32_t>(actor+0x58)=actor;
    append_process(0x0209b468u,actor+0x50);
    if (const uint32_t position=at<uint32_t>(0x0209b460u))
        std::memcpy(space.data(actor+0x5c,12),space.data(position,12),12);
    if (const uint32_t rotation=at<uint32_t>(0x0209b45cu)) {
        std::memcpy(space.data(actor+0x8c,6),space.data(rotation,6),6);
        std::memcpy(space.data(actor+0x92,6),space.data(rotation,6),6);
    }
    at<int8_t>(actor+0xcc)=at<int8_t>(0x0209b44cu);
    at<int16_t>(actor+0xce)=at<int16_t>(0x0208e378u);
    const uint32_t profile=at<uint32_t>(at<uint32_t>(0x020a4bb8u)+at<uint16_t>(actor+12)*4);
    at<uint32_t>(actor+0xb0)=at<uint32_t>(profile+8);
    uint32_t distance=at<uint32_t>(profile+0x14);
    if (at<uint8_t>(0x0209f2d8u)==2) distance+=0x7d0000u;
    native_actor_set_ranges(actor,at<int32_t>(profile+12),at<int32_t>(profile+16),
        static_cast<int32_t>(distance),at<int32_t>(profile+24));
    return actor;
}
void stage_frame_construct(uint32_t frame,uint32_t vtable) {
    at<uint32_t>(frame)=vtable;
    at<int32_t>(frame+8)=0;
    at<int32_t>(frame+12)=0x1000;
}
void stage_anim_model_construct(uint32_t model) {
    native_simple_model_construct(model);
    at<uint32_t>(model)=0x0208e980u;
    stage_frame_construct(model+0x50,0x0208e9a4u);
    at<uint32_t>(model+0x60)=0;
}
uint32_t stage_anim_model_ctor(uint32_t model) {
    (void)packed::Space::current().data(model,0x64);
    stage_anim_model_construct(model);
    return model;
}
uint32_t stage_anim2_model_construct(uint32_t model) {
    (void)packed::Space::current().data(model,0x78);
    stage_anim_model_construct(model);
    at<uint32_t>(model)=0x0208e9b4u;
    at<uint32_t>(model+0x50)=0x0208e9d8u;
    at<uint32_t>(model+0x64)=0;
    stage_frame_construct(model+0x68,0x0208e7e4u);
    return model;
}
int stage_model_set_file(uint32_t model,uint32_t file,int a,int b) {
    const uint32_t callback=at<uint32_t>(at<uint32_t>(model)+8);
    return Ptr<int(uint32_t,uint32_t,int,int)>{callback}(model,file,a,b);
}
void stage_frame_flags(uint32_t frame,int flags) {
    at<uint32_t>(frame+4)=(at<uint32_t>(frame+4)&0x3fffffffu)|static_cast<uint32_t>(flags);
}
uint32_t stage_frame_count(uint32_t frame) {
    return ((at<uint32_t>(frame+4)&0x3fffffffu)<<4)>>16;
}
void stage_frame_set_animation(uint32_t frame,uint16_t count,int flags,int speed,uint16_t start) {
    at<uint32_t>(frame+4)=static_cast<uint32_t>(flags)|(uint32_t(count)<<12);
    at<uint32_t>(frame+8)=uint32_t(start)<<12;
    at<int32_t>(frame+12)=speed;
}
void stage_anim_model_set_animation(uint32_t model,uint32_t file,int flags,int speed,uint16_t start) {
    if (at<uint32_t>(model+0x60)==file) {
        stage_frame_flags(model+0x50,flags);
        at<int32_t>(model+0x5c)=speed;
    } else {
        at<uint32_t>(model+0x60)=file;
        stage_frame_set_animation(model+0x50,at<uint16_t>(file+2),flags,speed,start);
    }
}
void stage_texture_prepare(uint32_t model,uint32_t file) {
    Ptr<void(uint32_t,uint32_t)>(0x02046d50u)(model,file);
}
void stage_model_polygon_id(uint32_t model,int id) {
    Ptr<void(uint32_t,int)>(0x02046008u)(model+8,id);
}
int stage_shadow_set_file(uint32_t model,uint32_t file,int flags,int) {
    const uint32_t data=Ptr<uint32_t(uint32_t)>(0x02016e70u)(file);
    at<uint32_t>(model+8)=data;
    if (!data) return 0;
    Ptr<void(uint32_t)>(0x0204504cu)(data);
    if (flags) {
        const uint32_t count=at<uint32_t>(at<uint32_t>(data)+0x24);
        const uint32_t materials=at<uint32_t>(data+4);
        for (uint32_t i=0;i<count;++i) at<uint32_t>(materials+i*0x30+0x24)|=0x8000;
    }
    return 1;
}
int stage_shadow_init_cylinder(uint32_t model) {
    return stage_model_set_file(model,0x020ad560u,1,-1);
}
int stage_shadow_init_cuboid(uint32_t model) {
    return stage_model_set_file(model,0x020ad524u,1,-1);
}
uint32_t stage_player_body_model(uint32_t player,uint32_t requested,int drawing) {
    const uint8_t metal=at<uint8_t>(player+(drawing ? 0x6fa : 0x6f9));
    return metal ? 4 : requested;
}
void stage_bg_query_construct(uint32_t query,uint32_t vtable) {
    at<uint32_t>(query)=vtable;
    at<uint8_t>(query+4)=1;
    at<int32_t>(query+8)=-1;
    at<uint32_t>(query+12)=0;
}
void stage_bg_result_construct(uint32_t result,uint32_t vtable=0x02099368u) {
    at<uint32_t>(result)=vtable;
    at<uint32_t>(result+4)=0xfc0;
    at<uint32_t>(result+8)=0xff;
    for (unsigned off : {12u,16u,20u,32u,36u}) at<uint32_t>(result+off)=0;
    at<uint16_t>(result+24)=0xffff;
    at<uint16_t>(result+26)=0x18;
    at<int32_t>(result+28)=-1;
}
// dBgCh_Gnd/dBgCh/dBgPi retain their two original base subobjects. These
// primitives are shared by entrance setup and the per-frame ground queries.
uint32_t stage_ground_construct(uint32_t query) {
    (void)packed::Space::current().data(query,0x50);
    stage_bg_query_construct(query,0x02099264u);
    stage_bg_result_construct(query+0x10,0x02099274u);
    at<int32_t>(query+0x4c)=0x1f4000;
    return query;
}
void stage_ground_destroy(uint32_t query) {
    at<uint32_t>(query)=0x020991d8u;
    at<uint32_t>(query+0x10)=0x02099368u;
}
void stage_ground_set_owner_position(uint32_t query,uint32_t position,uint32_t owner) {
    auto &space=packed::Space::current();
    std::memcpy(space.data(query+0x38,12),space.data(position,12),12);
    at<uint32_t>(query+12)=owner;
    at<int32_t>(query+8)=owner ? at<int32_t>(owner+4) : -1;
}
bool stage_query_owned(uint32_t query,uint32_t owner) {
    return owner && at<uint32_t>(query+12) &&
        at<uint32_t>(query+12)==owner && at<uint32_t>(query+8)==at<uint32_t>(owner+4);
}
void stage_ground_reset(uint32_t query) {
    const uint32_t result=query+0x10;
    at<uint16_t>(result+0x18)=0xffff;
    at<uint16_t>(result+0x1a)=0x18;
    at<int32_t>(result+0x1c)=-1;
    at<uint32_t>(result+0x20)=at<uint32_t>(result+0x24)=0;
    at<int32_t>(query+0x44)=static_cast<int32_t>(0x80000000u);
    at<uint8_t>(query+0x48)=0;
}
void stage_surface_construct(uint32_t surface) {
    at<uint32_t>(surface)=0xfc0;at<uint32_t>(surface+4)=0xff;
    for (unsigned off : {8u,12u,16u}) at<uint32_t>(surface+off)=0;
}
void stage_surface_destroy(uint32_t surface) { (void)packed::Space::current().data(surface,0x14); }
uint32_t stage_line_construct(uint32_t query) {
    (void)packed::Space::current().data(query,0x78);
    stage_bg_query_construct(query,0x020992a4u);
    stage_bg_result_construct(query+0x10,0x020992b4u);
    for (unsigned off : {0x54u,0x58u,0x5cu,0x60u}) at<uint32_t>(query+off)=0;
    at<uint32_t>(query+0x64)=0x020994ccu;
    return query;
}
void stage_line_destroy(uint32_t query) {
    at<uint32_t>(query)=0x020991d8u;
    at<uint32_t>(query+0x10)=0x02099368u;
    at<uint32_t>(query+0x64)=0x020994ccu;
}
void stage_line_set(uint32_t query,uint32_t start,uint32_t end,uint32_t owner) {
    auto &space=packed::Space::current();
    std::memmove(space.data(query+0x38,12),space.data(start,12),12);
    std::memmove(space.data(query+0x44,12),space.data(end,12),12);
    at<uint32_t>(query+12)=owner;
    at<int32_t>(query+8)=owner ? at<int32_t>(owner+4) : -1;
    at<uint8_t>(query+0x50)=0;
    std::memcpy(space.data(query+0x54,12),space.data(query+0x44,12),12);
    const int distance=Ptr<int(uint32_t,uint32_t)>(0x0203cfdcu)(query+0x54,query+0x38);
    at<int32_t>(query+0x60)=distance;
    // dBgPi::Reset leaves its surface record intact.
    at<uint16_t>(query+0x28)=0xffff;at<uint16_t>(query+0x2a)=0x18;
    at<int32_t>(query+0x2c)=-1;
    at<uint32_t>(query+0x30)=at<uint32_t>(query+0x34)=0;
    for (unsigned i=0;i<3;++i) {
        const uint32_t sum=at<uint32_t>(start+i*4)+at<uint32_t>(end+i*4);
        at<int32_t>(query+0x68+i*4)=static_cast<int32_t>(sum)>>1;
    }
    at<int32_t>(query+0x74)=(distance>>1)+0x1000;
}
void stage_line_position(uint32_t output,uint32_t query) {
    auto &space=packed::Space::current();
    std::memcpy(space.data(output,12),space.data(query+0x54,12),12);
}
bool stage_wall_facing(int normal) { return normal<0x600 && normal>-0xccc; }
void stage_result_set_collider(uint32_t result,int index,int id,uint32_t owner,uint32_t collider) {
    at<uint16_t>(result+0x1a)=static_cast<uint16_t>(index);
    at<int32_t>(result+0x1c)=id;
    at<uint32_t>(result+0x20)=owner;
    at<uint32_t>(result+0x24)=collider;
}
void stage_result_record(uint32_t result,int16_t triangle,uint32_t surface) {
    at<uint16_t>(result+0x18)=static_cast<uint16_t>(triangle);
    auto &space=packed::Space::current();
    std::memmove(space.data(result+4,0x14),space.data(surface,0x14),0x14);
}
uint32_t stage_collider_owner(uint32_t collider) { return at<uint32_t>(collider+4); }
int stage_collider_id(uint32_t collider) { return at<int32_t>(collider+8); }
int stage_collider_radius(uint32_t collider) { return at<int32_t>(collider+12); }
int stage_collider_offset(uint32_t collider) { return at<int32_t>(collider+16); }
bool stage_near_zero(int value) { return value>=-8 && value<=8; }
int stage_vector_distance(uint32_t a,uint32_t b) {
    uint64_t sum=0;
    for (unsigned off : {0u,4u,8u}) {
        const int32_t difference=static_cast<int32_t>(at<uint32_t>(a+off)-at<uint32_t>(b+off));
        sum+=uint64_t(int64_t(difference)*difference);
    }
    return cstd::sqrt(sum);
}
int stage_vector_horizontal_distance(uint32_t a,uint32_t b) {
    uint64_t sum=0;
    for (unsigned off : {0u,8u}) {
        const int32_t difference=static_cast<int32_t>(at<uint32_t>(a+off)-at<uint32_t>(b+off));
        sum+=uint64_t(int64_t(difference)*difference);
    }
    return cstd::sqrt(sum);
}
int16_t stage_vector_horizontal_angle(uint32_t a,uint32_t b) {
    const int32_t x=static_cast<int32_t>(at<uint32_t>(b)-at<uint32_t>(a));
    const int32_t z=static_cast<int32_t>(at<uint32_t>(b+8)-at<uint32_t>(a+8));
    return cstd::atan2(x,z);
}
int16_t stage_vector_vertical_angle(uint32_t a,uint32_t b) {
    const int32_t y=static_cast<int32_t>(at<uint32_t>(a+4)-at<uint32_t>(b+4));
    return cstd::atan2(y,stage_vector_horizontal_distance(a,b));
}
int16_t stage_triangle_index(uint32_t collider,uint32_t triangle) {
    const uint32_t file=at<uint32_t>(collider+0x20);
    return static_cast<int16_t>((triangle-at<uint32_t>(file+8))>>4);
}
void stage_kcl_normal(uint32_t collider,int16_t triangle,uint32_t output) {
    const uint32_t file=at<uint32_t>(collider+0x20);
    const uint32_t tri=at<uint32_t>(file+8)+uint32_t(int32_t(triangle))*0x10u;
    const uint32_t normal=at<uint32_t>(file+4)+at<uint16_t>(tri+6)*6u;
    for (unsigned i=0;i<3;++i) at<int32_t>(output+i*4)=int32_t(at<int16_t>(normal+i*2))*4;
}
void stage_kcl_surface(uint32_t collider,int16_t triangle,uint32_t output) {
    const uint32_t normalCallback=at<uint32_t>(at<uint32_t>(collider)+16);
    Ptr<void(uint32_t,int16_t,uint32_t)>{normalCallback}(collider,triangle,output+8);
    const uint32_t file=at<uint32_t>(collider+0x20);
    const uint32_t tri=at<uint32_t>(file+8)+uint32_t(int32_t(triangle))*0x10u;
    const uint32_t block=at<uint32_t>(collider+0x24);
    uint32_t entry;
    if (!block || at<uint16_t>(block+4)!=8) {
        entry=0x020a0c78u;
        at<uint32_t>(entry)=0xfc0;
        at<uint32_t>(entry+4)=0xff;
    } else entry=block+8+at<uint16_t>(tri+14)*8u;
    at<uint32_t>(output)=at<uint32_t>(entry);
    at<uint32_t>(output+4)=at<uint32_t>(entry+4);
}
bool stage_surface_pass(uint32_t,uint32_t surface,uint32_t query,bool check14) {
    const uint32_t properties=at<uint32_t>(surface);
    const uint8_t flags=at<uint8_t>(query+4);
    bool special=false;
    if (properties&0x20u) { special=true; if (!(flags&2)) return true; }
    if (properties&0x2000000u) { special=true; if (!(flags&8)) return true; }
    if (properties&0x1000000u) { special=true; if (flags&4) return true; }
    if (properties&0x4000000u) { special=true; if (!(flags&4)) return true; }
    const uint32_t type=(properties>>19)&0x1fu;
    if (type==0x11) { special=true; if (flags&0x20) return true; }
    if (check14 && type==0x14) { special=true; if (flags&0x40) return true; }
    return !special && !(flags&1);
}
void stage_query_water(uint32_t query) { at<uint8_t>(query+4)|=2; }
void stage_query_stop_vanish(uint32_t query) { at<uint8_t>(query+4)&=~0x20u; }
void stage_query_start_vanish(uint32_t query) { at<uint8_t>(query+4)|=0x20; }
void stage_query_special(uint32_t query) { at<uint8_t>(query+4)|=4; }
void stage_player_disable_interaction(uint32_t player) {
    at<uint8_t>(player+0x709)=1;
    at<uint32_t>(player+0x2d4+0x18)|=4;
}
void stage_mesh_query_construct(uint32_t query) {
    (void)packed::Space::current().data(query,0x1bc);
    stage_bg_query_construct(query,0x02099204u);
    const uint32_t sphere=query+0x20,line=query+0x134;
    stage_bg_query_construct(sphere,0x02099338u);
    stage_bg_result_construct(sphere+0x10,0x02099348u);
    at<uint32_t>(sphere+0x38)=0x02099358u;
    for (unsigned off : {0x74u,0x9cu,0xc4u}) stage_bg_result_construct(sphere+off);
    at<uint32_t>(sphere+0xec)=0;
    stage_bg_query_construct(line,0x020992a4u);
    stage_bg_result_construct(line+0x10,0x020992b4u);
    for (unsigned off : {0x54u,0x58u,0x5cu,0x60u}) at<uint32_t>(line+off)=0;
    at<uint32_t>(line+0x64)=0x020994ccu;
}
uint32_t stage_player_construct(uint32_t player) {
    auto &space=packed::Space::current();
    (void)space.data(player,0x768);
    stage_actor_construct(player);
    at<uint32_t>(player)=0x0210a83cu;
    for (unsigned off : {0xf0u,0x174u}) stage_anim_model_construct(player+off);
    for (unsigned i=0;i<4;++i) {
        stage_frame_construct(player+0x1dc+i*0x14,0x0208e7d4u);
        at<uint32_t>(player+0x1ec+i*0x14)=0;
    }
    for (unsigned i=0;i<2;++i) {
        stage_frame_construct(player+0x22c+i*0x14,0x0208e7f4u);
        at<uint32_t>(player+0x23c+i*0x14)=0;
        stage_frame_construct(player+0x254+i*0x14,0x0208e7d4u);
        at<uint32_t>(player+0x264+i*0x14)=0;
    }
    const uint32_t shadow=player+0x2ac;
    at<uint32_t>(shadow)=0x0208e868u;
    for (unsigned off : {4u,12u,32u,36u}) at<uint32_t>(shadow+off)=0;
    for (unsigned off : {0x2d4u,0x314u}) {
        const uint32_t collision=player+off;
        at<uint32_t>(collision)=0x0208e704u;
        for (unsigned lane : {0x28u,0x2cu,0x30u}) at<uint32_t>(collision+lane)=0;
    }
    stage_mesh_query_construct(player+0x380);
    return player;
}
uint32_t stage_player_factory() {
    return stage_player_construct(packed::Space::current().allocate(0x768));
}
uint32_t stage_camera_factory() {
    auto &space=packed::Space::current();
    const uint32_t camera=space.allocate(0x1a8);
    native_fbase_construct(camera);
    at<uint32_t>(camera)=0x02092720u;
    std::memset(space.data(camera+0x50,0x30),0,0x30);
    for (unsigned off : {0x50u,0x60u,0x70u}) at<int32_t>(camera+off)=0x1000;
    at<uint32_t>(camera)=0x02086f84u;
    return camera;
}
int stage_camera_state_init(uint32_t camera) {
    auto &flags=at<uint32_t>(camera+0x154);
    if (flags&0x4000) flags=(flags&~0x4000u)|0x8000;
    const uint32_t state=at<uint32_t>(camera+0x138);
    if (!at<uint32_t>(state)) return 1;
    packed::MemberCall member{};
    std::memcpy(&member,packed::Space::current().data(state,8),8);
    const auto target=packed::Space::current().resolve_member(camera,member);
    using Callback=int (*)(uint32_t);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    return callback(target.object);
}
void stage_camera_set_pos(uint32_t camera,uint32_t position) {
    // Complete Camera::SetPos (0x0200ccac): copy the vector to +0x8c.
    std::memcpy(packed::Space::current().data(camera+0x8c,12),
                packed::Space::current().data(position,12),12);
}
void stage_camera_set_look_at(uint32_t camera,uint32_t target) {
    // Complete Camera::SetLookAt (0x0200ccc8): copy the vector to +0x80.
    std::memcpy(packed::Space::current().data(camera+0x80,12),
                packed::Space::current().data(target,12),12);
}
void stage_camera_set_flag_3(uint32_t camera) {
    // Complete Camera::SetFlag_3 (0x0200d048): set bit 3, then call
    // FUN_02029a68 (the r0 flag word it receives is unused by that body).
    at<uint32_t>(camera+0x154)|=8;
    Ptr<void()>(0x02029a68u)();
}
void stage_wipe_release() {
    // Complete FUN_02029a68 (d_s_stage_methods.cpp): when the active wipe is
    // the seventh 0x60-byte fader after data_0209f324, clear it and BLDCNT.
    // Camera::SetFlag_3 passes a flag word in r0 that this body never reads.
    if (at<uint32_t>(0x0209d4b0u)==at<uint32_t>(0x0209f324u)+0x240u) {
        at<uint32_t>(0x0209d4b0u)=0;
        ntr::io_write(0x04000050u,0,2);
    }
    at<uint8_t>(0x0209f274u)=0;
}
int stage_player_level_enter_cleanup(uint32_t) {
    // Complete Player::St_LevelEnter_Cleanup (ov002 0x020c6f34): returns 1.
    return 1;
}
uint32_t stage_closest_player(uint32_t actor) {
    // Complete dActor_c::ClosestPlayer (0x02010ad8): the per-frame cache at
    // 0x0209b458 is filled once with the nearest and farthest players.
    if (!at<uint32_t>(0x0209b458u)) {
        at<int32_t>(0x0208e380u)=0x7fffffff;
        at<int32_t>(0x0208e37cu)=int32_t(0x80000000u);
        for (unsigned i=0;i<at<uint8_t>(0x0209f21cu);++i) {
            const uint32_t player=at<uint32_t>(0x0209f394u+i*4);
            if (!player) continue;
            const int32_t d=Ptr<int32_t(uint32_t,uint32_t)>(0x0203cfdcu)(actor+0x5c,player+0x5c);
            if (d<at<int32_t>(0x0208e380u)) { at<int32_t>(0x0208e380u)=d; at<uint32_t>(0x0209b458u)=player; }
            if (d>at<int32_t>(0x0208e37cu)) { at<int32_t>(0x0208e37cu)=d; at<uint32_t>(0x0209b450u)=player; }
        }
    }
    return at<uint32_t>(0x0209b458u);
}
int stage_camera_change_state(uint32_t camera,uint32_t state) {
    if (at<uint32_t>(camera+0x154)&0x10) return 0;
    if (at<uint32_t>(camera+0x138)!=state) {
        if (at<uint32_t>(camera+0x138)==0x0209b0c8u) {
            Ptr<void()>(0x02029a68u)();
            Ptr<void(uint32_t)>(0x020089f8u)(camera);
        }
        at<uint32_t>(camera+0x138)=state;
        at<uint8_t>(camera+0x1a6)=0;
    }
    return stage_camera_state_init(camera);
}
void stage_camera_player_reset(uint32_t camera,int player) {
    // Complete func_0200d81c: only the local player's camera enters state
    // 0x0209b008 through the original Camera::ChangeState.
    if (player!=int(at<uint8_t>(0x0209f250u))) return;
    (void)stage_camera_change_state(camera,0x0209b008u);
}
// Factories mirror the source constructors' original allocation, member
// construction order and vtables. All storage keeps cartridge-width handles.
template<uint32_t Size,uint32_t Vtable>
uint32_t stage_plain_actor_factory() {
    const uint32_t actor=packed::Space::current().allocate(Size);
    stage_actor_construct(actor);
    at<uint32_t>(actor)=Vtable;
    return actor;
}
void stage_shadow_construct(uint32_t model) {
    at<uint32_t>(model)=0x0208e868u;
    for (unsigned off : {4u,12u,32u,36u}) at<uint32_t>(model+off)=0;
}
void stage_actor_cylinder_construct(uint32_t collider,bool position) {
    at<uint32_t>(collider)=position ? 0x0208e704u : 0x0208e6d4u;
    for (unsigned off : {0x28u,0x2cu,0x30u}) at<uint32_t>(collider+off)=0;
}
uint32_t stage_door_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x148,0x02148188u>();
    stage_anim_model_construct(actor+0xd4);
    return actor;
}
uint32_t stage_castle_flag_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x138,0x02113ba0u>();
    stage_anim_model_construct(actor+0xd4);
    return actor;
}
uint32_t stage_tree_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x264,0x0210ac00u>();
    for (unsigned i=0;i<5;++i) native_simple_model_construct(actor+0xd4+i*0x50);
    return actor;
}
uint32_t stage_bird_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x184,0x02113958u>();
    stage_anim_model_construct(actor+0xd4);
    stage_shadow_construct(actor+0x138);
    return actor;
}
uint32_t stage_butterfly_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x3f4,0x02147e9cu>();
    stage_anim_model_construct(actor+0xd4);
    native_simple_model_construct(actor+0x138);
    stage_shadow_construct(actor+0x188);
    stage_shadow_construct(actor+0x1b0);
    stage_mesh_query_construct(actor+0x1d8);
    stage_actor_cylinder_construct(actor+0x394,true);
    return actor;
}
uint32_t stage_cannon_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x198,0x0213c6a0u>();
    native_simple_model_construct(actor+0xd4);
    stage_actor_cylinder_construct(actor+0x124,false);
    return actor;
}
uint32_t stage_fish_factory() {
    const uint32_t actor=stage_plain_actor_factory<0x160,0x021484bcu>();
    stage_anim_model_construct(actor+0xd4);
    at<int32_t>(actor+0x138)=1; // daFish_c's out-of-line constructor seed
    return actor;
}
uint32_t stage_bg_actor_construct(uint32_t actor) {
    stage_actor_construct(actor);
    at<uint32_t>(actor)=0x0210ae38u;
    native_simple_model_construct(actor+0xd4);
    native_mesh_collider_construct(actor+0x124);
    at<uint32_t>(actor+0x124)=0x02099434u;
    return actor;
}
uint32_t stage_metalnet_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x320);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x02113ae0u;
    return actor;
}
uint32_t stage_castle_water_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x338);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x02113a18u;
    stage_frame_construct(actor+0x320,0x0208e7c4u);
    at<uint32_t>(actor+0x330)=0;
    return actor;
}
uint32_t stage_sign_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x368);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x02130400u;
    stage_actor_cylinder_construct(actor+0x320,true);
    return actor;
}
uint32_t stage_signpost_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x5a4);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x02109af8u;
    stage_actor_cylinder_construct(actor+0x320,false);
    stage_shadow_construct(actor+0x358);
    stage_mesh_query_construct(actor+0x3c8);
    return actor;
}
uint32_t stage_hatena_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x3f8);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x0214e47cu;
    stage_anim_model_construct(actor+0x320);
    stage_shadow_construct(actor+0x384);
    return actor;
}
uint32_t stage_mushroom_factory() {
    const uint32_t actor=packed::Space::current().allocate(0x398);
    // dEnemyBase_c's empty-body C2 adds only its vptr after dActor_c.
    stage_actor_construct(actor);
    at<uint32_t>(actor)=0x021081e4u;
    at<uint32_t>(actor)=0x021083c8u;
    stage_actor_cylinder_construct(actor+0x110,false);
    stage_mesh_query_construct(actor+0x144);
    native_simple_model_construct(actor+0x300);
    stage_shadow_construct(actor+0x350);
    return actor;
}
void stage_shadow_init_model(uint32_t shadow,uint32_t matrix,int32_t x,int32_t y,int32_t z,uint8_t opacity) {
    // Complete ShadowModel::InitModel (0x02015e64): store the frame inputs
    // and push the shadow onto the per-frame list unless it is suspended.
    at<uint32_t>(shadow+0x0c)=matrix;
    at<int32_t>(shadow+0x10)=x; at<int32_t>(shadow+0x14)=y; at<int32_t>(shadow+0x18)=z;
    at<uint8_t>(shadow+0x1c)=opacity;
    if (at<uint8_t>(0x0209ceecu)) return;
    const uint32_t head=at<uint32_t>(0x0209cef4u);
    at<uint32_t>(shadow+0x24)=head;
    if (head) at<uint32_t>(head+0x20)=shadow;
    at<uint32_t>(0x0209cef4u)=shadow;
}
void stage_drop_shadow_rad_height(uint32_t actor,uint32_t shadow,uint32_t matrix,
                                  int32_t radius,int32_t depth,uint8_t opacity) {
    // Complete dActor_c::DropShadowRadHeight (0x02010be8).
    if (at<uint32_t>(actor+0xb0)&0x10) return;
    stage_shadow_init_model(shadow,matrix,radius,depth,radius,opacity);
}
int stage_mip_set_state(uint32_t actor,uint32_t record) {
    // Complete daMip_c::SetState (ov085 0x0212bc78): mState at +0x364.
    auto &space=packed::Space::current();
    at<uint32_t>(actor+0x364)=record;
    if (!at<uint32_t>(record)) return 1;
    packed::MemberCall member{};
    std::memcpy(&member,space.data(record,8),8);
    const auto target=space.resolve_member(actor,member);
    using Callback=int (*)(uint32_t);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    return callback(target.object);
}
int stage_mbg_detect_ground(uint32_t self,uint32_t ray) {
    // Complete dBgW_KcMbg::DetectClsn(dBgCh_Gnd &) (0x02039cb8, recovered
    // isolated unit): test the ground probe in the mesh's local space via
    // the shared line query at 0x020a0d0c, then map the hit back to world.
    auto &space=packed::Space::current();
    int32_t probe[3],lineEnd[3],localStart[3],localEnd[3],world[3];
    std::memcpy(probe,space.data(ray+0x38,12),12);              // func_020374b8
    std::memcpy(lineEnd,probe,12);
    int32_t probeHeight=at<int32_t>(ray+0x4c);
    if (at<uint8_t>(ray+0x48)) {
        const int32_t distance=probe[1]-at<int32_t>(ray+0x44);
        if (distance<probeHeight) probeHeight=distance;
    }
    lineEnd[1]-=probeHeight;
    using Map=void(uint32_t,uint32_t,uint32_t);
    Ptr<Map>(0x02039e48u)(self,space.address(probe),space.address(localStart));
    Ptr<Map>(0x02039e48u)(self,space.address(lineEnd),space.address(localEnd));
    const uint32_t line=0x020a0d0cu;
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(
        line,space.address(localStart),space.address(localEnd),0);
    at<uint8_t>(line+4)=at<uint8_t>(ray+4);                      // func_02035394
    at<int32_t>(line+8)=at<int32_t>(ray+8);
    at<uint32_t>(line+12)=at<uint32_t>(ray+12);
    const int hit=Ptr<int(uint32_t,uint32_t)>(0x01ffb0fcu)(self,line);
    if (hit) {
        Ptr<Map>(0x02039e30u)(self,0x020a0d60u,space.address(world));
        // dBgPi copy assignment: members after the vptr (0x04..0x27).
        std::memcpy(space.data(ray+0x14,0x24),space.data(0x020a0d20u,0x24),0x24);
        at<int32_t>(ray+0x44)=world[1];
        at<uint8_t>(ray+0x48)=1;
    }
    return hit;
}
uint32_t stage_block_l_factory() {
    // Complete daObjBlockL_c_classInit_* (five identical ov002 factories):
    // operator new(0x330), dBgActor_c C2, then the daObjBlockL_c vtable.
    const uint32_t actor=packed::Space::current().allocate(0x330);
    stage_bg_actor_construct(actor);
    at<uint32_t>(actor)=0x02108adcu;
    return actor;
}
bool stage_block_l_has_angle_x(uint32_t actor) {
    // Complete daObjBlockL_c::HasNonzeroAngleX (0x020b36a0).
    return at<int16_t>(actor+0x8c)!=0;
}
int32_t stage_event_get_bit(uint32_t bit) {
    // Complete Event::GetBit (0x02029ee0).
    return at<int32_t>(0x0209f34cu)&int32_t(1u<<bit);
}
bool stage_player_is_state(uint32_t player,uint32_t state) {
    // Complete Player::IsState (0x020e308c): pointer identity of mState.
    return at<uint32_t>(player+0x370)==state;
}
int stage_player_is_state_entering_level(uint32_t player) {
    // Complete Player::IsStateEnteringLevel (0x020c7ed4).
    if (stage_player_is_state(player,0x021104b4u)) return 1;
    return stage_player_is_state(player,0x0211025cu);
}
int stage_player_is_entering_level(uint32_t player) {
    // Complete Player::IsEnteringLevel (0x020c7e84).
    if (!stage_player_is_state_entering_level(player)) return 0;
    const uint8_t step=at<uint8_t>(player+0x6e3);
    return step==8||step==9||step==0x10;
}
void stage_sound_load_initial_group(int group) {
    // Complete Sound::LoadInitialGroup (0x02011ffc).
    Ptr<void(int,int)>(0x0201200cu)(group,0x20);
}
uint32_t stage_minimap_factory() {
    // Complete dMap_c_classInit (ov002 0x020fb8bc): operator new(0x258),
    // fBase_c C2, the dBase_c vtable, then the Minimap vtable.
    const uint32_t map=packed::Space::current().allocate(0x258);
    native_fbase_construct(map);
    at<uint32_t>(map)=0x0208e4b8u;
    at<uint32_t>(map)=0x0210c1c0u;
    return map;
}
int16_t stage_read_unaligned_short(uint32_t p) {
    return int16_t(at<uint8_t>(p)|(at<uint8_t>(p+1)<<8));
}
uint32_t stage_read_unaligned_int(uint32_t p) {
    return at<uint8_t>(p)|(at<uint8_t>(p+1)<<8)|(at<uint8_t>(p+2)<<16)|(uint32_t(at<uint8_t>(p+3))<<24);
}
// Original virtual dispatch: the DS vtable word at slot*4, receiver in r0.
template<class R,class... A> R stage_virtual(uint32_t object,unsigned slot,A... args) {
    return Ptr<R(uint32_t,A...)>(at<uint32_t>(at<uint32_t>(object)+slot*4))(object,args...);
}
void stage_process_kuppa_script() {
    // Complete ProcessKuppaScript (0x0200e8d4, recovered isolated unit).
    // FaderBrightness slots: 3 SetBackwardTime, 4 SetForwardTime,
    // 8 SetToEnd, 9 SetToStart; data_0209f5bc slot 7.
    uint32_t s=at<uint32_t>(0x0209fc48u);
    if (!s) return;
    uint32_t next=0;
    int fin=1;
    const uint32_t fader=0x0209b294u;
    while (at<uint8_t>(s)) {
        int beg=stage_read_unaligned_short(s+2);
        const int end=stage_read_unaligned_short(s+4);
        const uint16_t t=at<uint16_t>(0x0209b274u);
        if (beg>t) {
            fin=0;
        } else if (end<0 || end>=t) {
            const uint8_t cmd=at<uint8_t>(s+1);
            if (cmd==5) {
                if (stage_virtual<int>(at<uint32_t>(0x0209f5bcu),7)==0) {
                    const uint32_t i=at<uint8_t>(0x020a0e40u),off=i*4;
                    if (at<uint16_t>(0x020a0e58u+i*4+2)!=0 ||
                        (at<uint8_t>(0x020a0de8u+off)!=0 && at<uint8_t>(0x020a0de8u+off+1)!=0)) {
                        Ptr<void(uint32_t,uint32_t,uint16_t)>(0x0202e348u)(1,0,0);
                        Ptr<void(uint32_t)>(0x02013050u)(0x3c);
                    }
                }
                fin=0;
            } else if (cmd==6) {
                next=stage_read_unaligned_int(s+6);
            } else if (cmd==7) {
                Ptr<void(int)>(0x0201320cu)(int(stage_read_unaligned_int(s+6)));
            } else if (cmd==8) {
                Ptr<void(uint32_t,uint32_t)>(0x02012840u)(1,stage_read_unaligned_int(s+6));
            } else if (cmd==9) {
                (void)Ptr<uint32_t(uint32_t)>(0x02012790u)(stage_read_unaligned_int(s+6));
            } else if (cmd==0xa) {
                if (beg==t) {
                    beg=stage_read_unaligned_short(s+6);
                    if (beg==0xd && Ptr<int(uint32_t)>(0x0201392cu)(1)==0 &&
                        Ptr<int(uint32_t)>(0x0201392cu)(2)==0)
                        beg=0x16;
                    Ptr<void(int)>(0x0201f32cu)(beg);
                } else if (end==t) {
                    at<uint8_t>(0x0209d66cu)=1;
                }
            } else if (cmd==0xb) {
                Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x0202acc0u)(
                    uint32_t(int32_t(at<int8_t>(s+6))),at<uint8_t>(s+7),uint32_t(int32_t(at<int8_t>(s+8))),0);
                at<uint32_t>(0x0209fc4cu)=stage_read_unaligned_int(s+9);
                if (at<uint8_t>(0x0209f2d8u)!=2) Ptr<void(uint32_t)>(0x02013050u)(0x3c);
                Ptr<void()>(0x0202e1b4u)();
                at<uint32_t>(0x0209d4b0u)=0;
            } else if (cmd>=0xc && cmd<=0xf) {
                // 0xc/0xe start at the beginning and run forward; 0xd/0xf
                // start at the end and run backward. 0xc/0xd fade white.
                const bool backward=cmd==0xd||cmd==0xf;
                if (cmd==0xf) at<int8_t>(0x0209d45cu)=0x10;
                stage_virtual<void>(fader,backward?8:9);
                at<uint16_t>(fader+0xc)=cmd<=0xd?0x7fff:0;
                stage_virtual<int,unsigned,unsigned>(fader,backward?3:4,
                    cmd==0xc?0xeu:cmd==0xe?0x1eu:0x50u,0u);
                at<uint32_t>(0x0209d4b0u)=fader;
            } else if (cmd==0x10) {
                stage_virtual<void>(fader,8);
                at<uint16_t>(fader+0xc)=0;
                stage_virtual<int,unsigned,unsigned>(fader,4,0x1eu,0u);
                Ptr<void(uint32_t)>(0x0202e1e0u)(fader);
            } else if (cmd==0x11) {
                at<uint32_t>(0x02110aecu)=0;
            } else if (cmd==4) {
                (void)Ptr<int(uint32_t,uint32_t,int,int)>(0x02008550u)(at<uint32_t>(0x0209f318u),s,beg,end);
            } else if (cmd<=3) {
                (void)Ptr<int(uint32_t,int,int)>(0x0200e5acu)(s,beg,end);
            } else if (cmd>=0x12) {
                (void)Ptr<int(uint32_t,int,int)>(0x0200e494u)(s,beg,end);
            }
            if (end>=0) fin=0;
        }
        s+=at<uint8_t>(s);
    }
    if (next) {
        at<uint32_t>(0x0209fc48u)=next;
        at<uint16_t>(0x0209b274u)=0;
        return;
    }
    if (fin && at<int8_t>(0x02092110u)<0) {
        Ptr<void()>(0x0200e798u)();
        return;
    }
    auto &timer=at<uint16_t>(0x0209b274u);
    ++timer;
    if (timer==0) --timer;
}
uint32_t stage_demo_factory() {
    // Complete daDemo_c_classInit (ov002 0x020f8808): operator new(0x104),
    // dActor_c C2, then the CutsceneObject vtable.
    const uint32_t actor=packed::Space::current().allocate(0x104);
    stage_actor_construct(actor);
    at<uint32_t>(actor)=0x0210bd60u;
    return actor;
}
// Guarded function-local static PMF tables (recovered isolated units): on
// first use the eight-byte PMFs are copied from their ROM constants, then
// entry p[6] runs on self with (p+7,a2,a3).
template<size_t N>
int stage_static_pmf_dispatch(uint32_t guardAddress,uint32_t table,const uint32_t (&sources)[N],
                               uint32_t self,uint32_t p,int a2,int a3) {
    auto &space=packed::Space::current();
    auto &guard=at<uint32_t>(guardAddress);
    if (!(guard&1)) {
        guard|=1;
        for (unsigned i=0;i<N;++i)
            std::memcpy(space.data(table+i*8,8),space.data(sources[i],8),8);
    }
    const unsigned index=at<uint8_t>(p+6);
    if (index>=N) throw std::runtime_error("Original command PMF index out of range.");
    packed::MemberCall member{};
    std::memcpy(&member,space.data(table+index*8,8),8);
    const auto target=space.resolve_member(self,member);
    // The int-returning callee form; void callees leave r0 unused.
    using Callback=int (*)(uint32_t,uint32_t,int,int);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    return callback(target.object,p+7,a2,a3);
}
int stage_camera_dispatch_command(uint32_t self,uint32_t p,int a2,int a3) {
    // Complete func_02008550 (camera cutscene commands, 39 PMFs).
    static constexpr uint32_t sources[39]={0x02086c80u,0x02086d80u,0x02086c60u,0x02086c90u,0x02086bd8u,0x02086d18u,0x02086d98u,0x02086c88u,0x02086d00u,0x02086bc0u,0x02086e30u,0x02086ce8u,0x02086e38u,0x02086e20u,0x02086cd0u,0x02086e08u,0x02086df0u,0x02086cb8u,0x02086dd8u,0x02086dc0u,0x02086c20u,0x02086da0u,0x02086d60u,0x02086d88u,0x02086cf8u,0x02086d68u,0x02086c78u,0x02086d40u,0x02086d38u,0x02086d20u,0x02086c50u,0x02086d08u,0x02086cf0u,0x02086c38u,0x02086cd8u,0x02086cc0u,0x02086cb0u,0x02086ca8u,0x02086ca0u};
    return stage_static_pmf_dispatch(0x0209b004u,0x0209b138u,sources,self,p,a2,a3);
}
void stage_demo_dispatch_command(uint32_t self,uint32_t p,int a2,int a3) {
    // Complete func_ov002_020f7d74 (cutscene object commands).
    static constexpr uint32_t sources[24]={
        0x0210b708u,0x0210b678u,0x0210b618u,0x0210b620u,0x0210b640u,0x0210b628u,
        0x0210b648u,0x0210b8c0u,0x0210b900u,0x0210b938u,0x0210b930u,0x0210b928u,
        0x0210b920u,0x0210b918u,0x0210b910u,0x0210b908u,0x0210b750u,0x0210b8f8u,
        0x0210b8f0u,0x0210b8e8u,0x0210b8e0u,0x0210b8d8u,0x0210b8d0u,0x0210b8c8u};
    stage_static_pmf_dispatch(0x02110b00u,0x0211104cu,sources,self,p,a2,a3);
}
void stage_player_dispatch_command(uint32_t self,uint32_t p,int a2,int a3) {
    // Complete func_ov002_020bd664 (Player cutscene commands).
    static constexpr uint32_t sources[14]={
        0x0210a14cu,0x0210a124u,0x0210a054u,0x0210a0dcu,0x0210a534u,0x0210a0b4u,
        0x0210a094u,0x0210a40cu,0x0210a474u,0x0210a44cu,0x0210a3fcu,0x0210a3c4u,
        0x0210a064u,0x0210a36cu};
    stage_static_pmf_dispatch(0x0210e158u,0x02110794u,sources,self,p,a2,a3);
}
int stage_demo_bg_thunk(uint32_t c) {
    // Complete func_ov002_020f26d4: a register-preserving tail call
    // (ldr ip / bx ip) into func_ov002_020f2630 with the same r0.
    return Ptr<int(uint32_t)>(0x020f2630u)(c);
}
// NNS sound-player boundary of the stage address space. The original
// Sound:: bodies above this boundary run unchanged; the SDK player handles
// map onto native sequencer players: 0x0209b4a0 (music layer 1) -> 0 and
// 0x0209b4b0 (music layer 2) -> 4. Unknown handles fail explicitly.
int stage_sound_player(uint32_t handle) {
    if (handle==0x0209b4a0u) return 0;
    if (handle==0x0209b4b0u) return 4;
    throw std::runtime_error("Unmapped original NNS sound handle: "+std::to_string(handle));
}
int stage_snd_start_seq(uint32_t handle,int sequence) {
    // NNS_SndArcPlayerStartSeq (0x02051fb4).
    return GameProgram::active().native_audio({SoundOp::Music,stage_sound_player(handle),0,sequence})?1:0;
}
int stage_snd_stop_seq(uint32_t handle,int frames) {
    // NNS_SndPlayerStopSeq (0x0204fa2c).
    GameProgram::active().native_audio({SoundOp::Stop,stage_sound_player(handle),0,frames});
    return 1;
}
void stage_snd_move_volume(uint32_t handle,int volume,int frames) {
    // NNS_SndPlayerMoveVolume (0x0204f8cc).
    GameProgram::active().native_audio({SoundOp::Fade,stage_sound_player(handle),0,volume,frames});
}
void stage_sound_load_and_set_music_layer1(int music) {
    // Complete Sound::LoadAndSetMusic_Layer1 (0x0201320c).
    at<int32_t>(0x0208e438u)=-1;
    at<int32_t>(0x0208e434u)=-1;
    at<int32_t>(0x0209b48cu)=0;
    if (music<0) {
        stage_snd_stop_seq(0x0209b4a0u,0);
    } else {
        if (!(at<int32_t>(0x0208e430u)>=0 && at<int32_t>(0x0209b4acu)>=0 && at<int32_t>(0x0208e43cu)==music))
            (void)Ptr<int(uint32_t,int)>(0x02011dccu)(0x0209b4a0u,music);
        Ptr<void(uint32_t,int,int)>(0x02013524u)(0x0209b4a0u,0x7f,0);
        stage_snd_stop_seq(0x0209b4b0u,0);
    }
    at<int32_t>(0x0209b4acu)=music;
    at<int32_t>(0x0208e43cu)=music;
    Ptr<void()>(0x02011b7cu)();
}
void stage_sound_stop_loaded_music_layer1(uint32_t frames) {
    // Complete Sound::StopLoadedMusic_Layer1 (0x02013050).
    at<uint32_t>(0x0209b4acu)=~0u;
    stage_snd_stop_seq(0x0209b4a0u,int(frames));
}
uint32_t stage_mip_factory() {
    // Complete daMip_c_classInit (ov085 0x0212cc2c): operator new(0x474),
    // dEnemyBase_c C2, then dCcAc_c, dBgCh_Actr, ModelAnim and two
    // ShadowModel member constructors before the final daMip_c vtable.
    const uint32_t actor=packed::Space::current().allocate(0x474);
    stage_actor_construct(actor);
    at<uint32_t>(actor)=0x021081e4u;
    stage_actor_cylinder_construct(actor+0x110,false);
    stage_mesh_query_construct(actor+0x144);
    stage_anim_model_construct(actor+0x300);
    stage_shadow_construct(actor+0x368);
    stage_shadow_construct(actor+0x3c0);
    at<uint32_t>(actor)=0x021300f8u;
    return actor;
}
void stage_texture_sequence_construct(uint32_t sequence) {
    // TextureSequence::TextureSequence: Animation C2, its vptr, file = 0.
    (void)packed::Space::current().data(sequence,0x14);
    stage_frame_construct(sequence,0x0208e7d4u);
    at<uint32_t>(sequence+0x10)=0;
}
uint32_t stage_texture_sequence_ctor(uint32_t sequence) {
    stage_texture_sequence_construct(sequence);
    return sequence;
}
uint32_t stage_jugem_factory() {
    // Complete daC_Jugem_c_classInit (ov085 0x0212ed54): operator new(0x2e8),
    // dEnemyBase_c C2, two ModelAnim, TextureSequence and two ShadowModel
    // member constructors, then the final daC_Jugem_c vtable.
    const uint32_t actor=packed::Space::current().allocate(0x2e8);
    stage_actor_construct(actor);
    at<uint32_t>(actor)=0x021081e4u;
    stage_anim_model_construct(actor+0x110);
    stage_anim_model_construct(actor+0x174);
    stage_texture_sequence_construct(actor+0x1d8);
    stage_shadow_construct(actor+0x1f0);
    stage_shadow_construct(actor+0x218);
    at<uint32_t>(actor)=0x02130344u;
    return actor;
}
int stage_jugem_set_state(uint32_t actor,uint32_t record) {
    // Complete daC_Jugem_c::SetState (ov085 0x0212e728): store the
    // {enter, run} record, then invoke its enter PMF when present.
    auto &space=packed::Space::current();
    at<uint32_t>(actor+0x1ec)=record;
    packed::MemberCall member{};
    std::memcpy(&member,space.data(record,8),8);
    if (!at<uint32_t>(record)) return 1;
    const auto target=space.resolve_member(actor,member);
    using Callback=int (*)(uint32_t);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    return callback(target.object);
}
void stage_actor_mesh_limit(uint32_t query) {
    (void)packed::Space::current().data(query,0x1bc);
    at<uint32_t>(query+0x10)|=0x80;
}
void stage_hatena_set_state(uint32_t actor,int state) {
    // Complete func_ov102_02149da8: store state, invoke its original enter PMF.
    auto &space=packed::Space::current();
    at<int32_t>(actor+0x3e8)=state;
    packed::MemberCall member{};
    std::memcpy(&member,space.data(0x0214e890u+uint32_t(state)*16u,8),8);
    const auto target=space.resolve_member(actor,member);
    using Callback=void (*)(uint32_t);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    callback(target.object);
}
void stage_actor_mesh_start_water(uint32_t query) {
    // dBgCh_Actr::StartDetectingWater, line first, then sphere.
    (void)packed::Space::current().data(query,0x1bc);
    stage_query_water(query+0x134);
    stage_query_water(query+0x20);
}
int stage_collider_is_enabled(uint32_t collider) { return at<uint8_t>(collider+20)!=24; }
int stage_collider_disable(uint32_t collider) {
    const uint8_t slot=at<uint8_t>(collider+20);
    if(slot==24) return 1;
    if(slot>24) throw std::runtime_error("Invalid mesh collider slot.");
    at<uint32_t>(0x020a0c80u+slot*4)=0;
    at<uint8_t>(collider+20)=24;
    return 1;
}
void stage_scale_matrix(uint32_t matrix,int x,int y,int z) {
    // func_020527e8: the original Thumb SDK scale-matrix primitive.
    auto &space=packed::Space::current();
    std::memset(space.data(matrix,48),0,48);
    at<int32_t>(matrix)=x;
    at<int32_t>(matrix+16)=y;
    at<int32_t>(matrix+32)=z;
}
uint32_t stage_position_cylinder_construct(uint32_t collider) {
    (void)packed::Space::current().data(collider,0x3c);
    at<uint32_t>(collider)=0x0208e6bcu;
    at<uint32_t>(collider+0x28)=at<uint32_t>(collider+0x2c)=0;
    return collider;
}
void stage_position_cylinder_init(uint32_t collider,uint32_t position,int radius,int height,
                                  uint32_t flags,uint32_t vulnerable) {
    auto &space=packed::Space::current();
    std::memcpy(space.data(collider+0x30,12),space.data(position,12),12);
    native_collision_init(collider,radius,height,flags,vulnerable);
}
uint32_t stage_position_cylinder_pos(uint32_t collider) { return collider+0x30; }
uint32_t stage_position_cylinder_owner(uint32_t) { return 0; }
void stage_centered_cylinder_init(uint32_t collider,uint32_t actor,int radius,int height,
                                  uint32_t flags,uint32_t vulnerable) {
    at<uint32_t>(collider+0x30)=actor;
    native_collision_init(collider,radius,height,flags,vulnerable);
}
uint32_t stage_door_relative_player(uint32_t door) {
    auto &space=packed::Space::current();
    const uint32_t player=at<uint32_t>(0x0209f394u+at<uint8_t>(0x0209f250u)*4u);
    int32_t relative[3];
    for (unsigned i=0;i<3;++i)
        relative[i]=static_cast<int32_t>(at<uint32_t>(player+0x5c+i*4)-at<uint32_t>(door+0x5c+i*4));
    const int16_t angle=static_cast<int16_t>(-int32_t(at<int16_t>(door+0x8e)));
    Ptr<void(uint32_t,uint32_t,int16_t,uint32_t)>(0x0203b6a4u)(
        door+0x80,0x020a0ebcu,angle,space.address(relative));
    return player;
}
int stage_door_change_state(uint32_t door,uint32_t state,int player) {
    auto &space=packed::Space::current();
    at<uint32_t>(door+0x140)=state;
    if (!at<uint32_t>(state)) return 1;
    packed::MemberCall member{};
    std::memcpy(&member,space.data(state,8),8);
    const auto target=space.resolve_member(door,member);
    using Callback=int (*)(uint32_t,int);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    return callback(target.object,player);
}
uint32_t stage_vector_lifetime(uint32_t vector) {
    // The original default Vector3 constructor and destructor have empty
    // bodies. Their ABI returns the existing object and writes no component.
    (void)packed::Space::current().data(vector,12);
    return vector;
}
void stage_actor_cylinder_init(uint32_t collider,uint32_t actor,uint32_t offset,
                                int radius,int height,uint32_t flags,uint32_t vulnerable) {
    (void)packed::Space::current().data(collider,0x40);
    at<uint32_t>(collider+0x30)=actor;
    native_collision_init(collider,radius,height,flags,vulnerable);
    Ptr<void(uint32_t,uint32_t,int16_t,uint32_t)>(0x0203b6a4u)(
        collider+0x34,actor+0x5c,at<int16_t>(actor+0x8e),offset);
}
void stage_actor_mesh_init(uint32_t query,uint32_t actor,int radius,int height,
                            uint32_t motionAngle,uint32_t angle) {
    (void)packed::Space::current().data(query,0x1bc);
    at<uint32_t>(query+0x14)=actor;
    at<int32_t>(query+0x18)=radius;
    at<int32_t>(query+0x1c)=height;
    at<uint32_t>(query+0x10)=0;
    at<uint32_t>(query+0x12c)=motionAngle;
    at<uint32_t>(query+0x130)=angle;
    at<int32_t>(query+0x1b8)=0x1000;
}
void stage_actor_wall_extent(uint32_t query,int extent) {
    at<int32_t>(query+0x10c)=extent;
}
int stage_player_change_state(uint32_t player,uint32_t next) {
    auto &space=packed::Space::current();
    (void)space.data(player,0x768);
    (void)space.data(next,24);
    at<uint32_t>(player+0x378)=next;
    const uint32_t current=at<uint32_t>(player+0x370);
    PlayerStateDispatch dispatch(space);
    if (current && at<uint32_t>(current+16) &&
        !dispatch.invoke(player,current,PlayerStatePhase::Cleanup)) return 0;
    if (current==0x0211022cu && !(at<uint16_t>(player+0x6ce)&0x400) &&
        at<uint8_t>(player+0x709) && next!=0x0211013cu && next!=0x0211067cu &&
        next!=0x021106acu && next!=0x0211022cu) return 0;
    if (at<uint32_t>(player+0x358) && next==0x0211013cu) {
        at<uint8_t>(player+0x6e3)=1;
        next=0x02110364u;
    }
    for (unsigned offset : {0x620u,0x628u,0x62cu,0x630u,0x690u,0x694u,0x654u})
        at<uint32_t>(player+offset)=0;
    at<uint32_t>(player+0x374)=current;
    at<uint32_t>(player+0x370)=next;
    for (unsigned offset : {0x717u,0x716u,0x712u,0x6f6u,0x6ecu,0x726u})
        at<uint8_t>(player+offset)=0;
    at<uint8_t>(player+0x713)=1;
    at<int16_t>(player+0x90)=at<int16_t>(player+0x8c)=0;
    at<uint16_t>(player+0x6ce)&=static_cast<uint16_t>(~0x200u);
    at<int32_t>(player+0x9c)=-0x4000;
    at<int32_t>(player+0xa0)=-0x4b000;
    at<uint16_t>(player+0x6a4)=0;
    // Complete func_ov002_020d4540 and Player::func_ov002_020c9e18.
    at<uint8_t>(player+0x6e0)=0;
    at<uint8_t>(player+0x708)=0;
    if (at<uint8_t>(player+0x709)==1) {
        at<uint8_t>(player+0x709)=0;
        at<uint32_t>(player+0x2d4+0x18)&=~4u;
    }
    Ptr<void(uint32_t)>(0x020e6780u)(player);
    if (const uint32_t camera=at<uint32_t>(0x0209f318u))
        Ptr<void(uint32_t,int)>(0x0200d81cu)(camera,at<uint8_t>(player+0x6d8));
    return dispatch.invoke(player,at<uint32_t>(player+0x370),PlayerStatePhase::Init);
}
void stage_player_entrance_state(uint32_t player,int entrance) {
    at<uint8_t>(player+0x6e3)=at<uint8_t>(0x020ff240u+static_cast<uint32_t>(entrance));
    const uint8_t reason=at<uint8_t>(0x0209f2fcu);
    const uint8_t step=at<uint8_t>(player+0x6e3);
    if (reason==2) {
        if (step==4 || step==8 || step==0xb) at<uint8_t>(player+0x6e3)=step+1;
        Ptr<void(uint32_t,int)>(0x020bf4e4u)(player,-0x880);
        Ptr<void(uint32_t,int)>(0x020bf4e4u)(player,0x100);
    } else if (reason==1 && step==4) at<uint8_t>(player+0x6e3)=0xf;
    (void)stage_player_change_state(player,0x021104b4u);
}
void stage_mark_for_destruction(uint32_t actor) {
    const uint8_t state=at<uint8_t>(actor+14);
    if (at<uint8_t>(actor+15) || state==2) return;
    at<uint8_t>(actor+15)=1;
    const uint32_t callback=at<uint32_t>(at<uint32_t>(actor)+0x30);
    Ptr<void(uint32_t)>{callback}(actor);
}
bool stage_actor_before_init(uint32_t actor) {
    // dActor_c::BeforeInitResources and GetBitInDeathTable, retaining the
    // original negative-ID guard inside DeathTable_GetBit itself.
    if (!(at<uint32_t>(actor+0xb0)&0x08000000u) &&
        Ptr<uint32_t(int)>(0x02029e58u)(at<int16_t>(actor+0xce))) {
        stage_mark_for_destruction(actor);
        return true;
    }
    return fbase_before_init(actor);
}
}

uint32_t native_mesh_collider_construct(uint32_t collider) {
    (void)packed::Space::current().data(collider,0x50);
    // dBgW::dBgW()/func_02039624, CLPS_BlockRef(), dBgW_Kc().
    at<uint32_t>(collider)=0x020993dcu;
    at<uint32_t>(collider+4)=0;
    at<int32_t>(collider+8)=-1;
    at<int32_t>(collider+12)=-0x1000;
    at<uint32_t>(collider+16)=0;
    at<uint8_t>(collider+20)=0x18;
    at<uint32_t>(collider+24)=0;
    at<uint32_t>(collider+28)=0;
    at<uint32_t>(collider+0x24)=0;
    at<uint32_t>(collider+0x20)=0;
    return collider;
}

uint32_t native_stage_factory() {
    auto &space=packed::Space::current();
    // dScStage_c_classInit: new dScStage_c. Its genuine EU allocation is
    // 0x9c8, including mWaitingForLoad at +0x9c4, not sizeof a host class.
    const uint32_t actor=space.allocate(0x9c8);
    native_fbase_construct(actor);
    at<uint32_t>(actor)=0x0208e4b8u; // dBase_c implicit constructor
    at<uint32_t>(actor)=0x02092680u; // dScene_c implicit constructor
    at<uint8_t>(actor+0x13)|=1;
    at<uint8_t>(actor+0x13)|=4;
    at<uint32_t>(actor)=0x020921c0u;
    native_particle_tracker_construct(actor+0x50);
    native_simple_model_construct(actor+0x86c);
    native_mesh_collider_construct(actor+0x91c);
    return actor;
}

void native_actor_post_construct(uint32_t actor) {
    auto &space=packed::Space::current();
    // func_0204335c uses these exact three ROM PMF records, not native PMFs.
    ActorProcessMethods methods{};
    std::memcpy(&methods.action,space.data(0x02099ebcu,8),8);
    std::memcpy(&methods.before,space.data(0x02099ec4u,8),8);
    std::memcpy(&methods.after,space.data(0x02099e94u,8),8);
    (void)ActorDispatch(space).process(actor,methods);
    if (at<uint8_t>(actor+15) || at<uint8_t>(actor+16) || at<uint8_t>(actor+14)!=0) return;
    if (at<uint32_t>(0x02099f24u)==2) {
        at<uint8_t>(actor+17)=1;
        return;
    }
    append_process(0x020a4b88u,actor+0x28);
}

namespace {
void stage_base_after_init(uint32_t actor,uint32_t status) {
    if (status!=2) return;
    unlink_process(0x020a4b88u,actor+0x28);
    if (at<uint32_t>(0x02099f24u)==3) { at<uint8_t>(actor+16)=1;return; }
    insert_process(0x020a4b78u,actor+0x28);
    insert_process(0x020a4b98u,actor+0x38);
    at<uint8_t>(actor+14)=1;
}
void stage_derived_after_init(uint32_t actor,uint32_t status) {
    if (status==1) stage_mark_for_destruction(actor);
    stage_base_after_init(actor,status);
}
void stage_actor_after_init(uint32_t actor,uint32_t status) {
    stage_derived_after_init(actor,status);
    at<uint32_t>(actor+0xb0)|=0x38;
}
int stage_process(uint32_t actor,uint32_t action,uint32_t before,uint32_t after) {
    auto &space=packed::Space::current();
    ActorProcessMethods methods{};
    std::memcpy(&methods.action,space.data(action,8),8);
    std::memcpy(&methods.before,space.data(before,8),8);
    std::memcpy(&methods.after,space.data(after,8),8);
    return ActorDispatch(space).process(actor,methods);
}
int stage_process_init(uint32_t actor) { return stage_process(actor,0x02099ebcu,0x02099ec4u,0x02099e94u); }
int stage_process_behavior(uint32_t actor) { return stage_process(actor,0x02099ea4u,0x02099eacu,0x02099eb4u); }
int stage_process_render(uint32_t actor) { return stage_process(actor,0x02099eccu,0x02099e74u,0x02099e9cu); }
int stage_process_cleanup(uint32_t actor) {
    const uint16_t id=at<uint16_t>(actor+12);
    const int result=stage_process(actor,0x02099e8cu,0x02099e7cu,0x02099e84u);
    if (result==1) Ptr<void(int)>(0x0204302cu)(id);
    return result;
}
int stage_reconcile_actor(uint32_t actor) {
    if (at<uint8_t>(actor+15)) {
        at<uint8_t>(actor+15)=0;
        if (at<uint8_t>(actor+14)==1) {
            unlink_process(0x020a4b78u,actor+0x28);
            unlink_process(0x020a4b98u,actor+0x38);
        } else unlink_process(0x020a4b88u,actor+0x28);
        // func_0203b20c prepends a newly dying actor.
        const uint32_t node=actor+0x28,head=at<uint32_t>(0x020a4ba8u);
        at<uint32_t>(node+4)=head;
        if (head) at<uint32_t>(head)=node;
        else at<uint32_t>(0x020a4bacu)=node;
        at<uint32_t>(0x020a4ba8u)=node;
        at<uint8_t>(actor+14)=2;
        for (uint32_t child=at<uint32_t>(actor+0x18);child;child=at<uint32_t>(child+12))
            stage_mark_for_destruction(at<uint32_t>(child+16));
    } else {
        const uint32_t parent=at<uint32_t>(actor+0x14);
        if (parent) {
            const uint32_t owner=at<uint32_t>(parent+16);
            const uint8_t flags=at<uint8_t>(owner+0x13);
            auto &own=at<uint8_t>(actor+0x13);
            own=uint8_t((own&~2u)|((flags&3) ? 2 : 0));
            own=uint8_t((own&~8u)|((flags&12) ? 8 : 0));
        }
        if (at<uint8_t>(actor+14)==1) {
            for (auto entry : {std::pair<uint32_t,uint32_t>{0x020a4b78u,actor+0x28},
                                {0x020a4b98u,actor+0x38}}) {
                if (at<uint16_t>(entry.second+12)!=at<uint16_t>(entry.second+14)) {
                    unlink_process(entry.first,entry.second);
                    at<uint16_t>(entry.second+12)=at<uint16_t>(entry.second+14);
                    insert_process(entry.first,entry.second);
                }
            }
        } else if (at<uint8_t>(actor+14)!=2) {
            if (at<uint8_t>(actor+17)) {
                at<uint8_t>(actor+17)=0;append_process(0x020a4b88u,actor+0x28);
            } else if (at<uint8_t>(actor+16)) {
                at<uint8_t>(actor+16)=0;
                insert_process(0x020a4b78u,actor+0x28);
                insert_process(0x020a4b98u,actor+0x38);
                at<uint8_t>(actor+14)=1;
            }
        }
    }
    return 1;
}
void stage_actor_frame() {
    // Complete func_02044120 order: cleanup, init, behavior, render, tree.
    for (auto phase : {std::pair<uint32_t,uint32_t>{4,0x020a4ba8u},
                       {2,0x020a4b88u},{3,0x020a4b78u},{5,0x020a4b98u}}) {
        at<uint32_t>(0x02099f24u)=phase.first;
        native_fbase_process_list(phase.second);
    }
    at<uint32_t>(0x02099f24u)=1;
    const uint32_t entry=at<uint32_t>(0x020a4b70u),adjust=at<uint32_t>(0x020a4b74u);
    if (entry || adjust) {
        uint32_t node=at<uint32_t>(0x020a4b6cu);
        std::unordered_set<uint32_t> visited;
        while (node) {
            if (!visited.insert(node).second) throw std::runtime_error("Cyclic actor scene traversal.");
            uint32_t next=at<uint32_t>(node+4),ancestor=node;
            while (!next && ancestor) {
                next=at<uint32_t>(ancestor+12);ancestor=at<uint32_t>(ancestor);
            }
            const auto target=packed::Space::current().resolve_member(
                at<uint32_t>(node+16),{entry,static_cast<int32_t>(adjust)});
            using Callback=void (*)(uint32_t);
            Callback callback=nullptr;
            std::memcpy(&callback,&target.code,sizeof callback);
            callback(target.object);
            node=next;
        }
    }
    at<uint32_t>(0x02099f24u)=0;
}
}

void bind_stage_overlay_runtime(packed::Space &space,unsigned id) {
    if(id==102) {
        for(uint32_t factory : {0x0214a7ecu,0x0214a82cu,0x0214a86cu,
                                0x0214a8acu,0x0214a8ecu,0x0214a92cu})
            space.bind(factory,&stage_hatena_factory);
        space.bind(0x02149da8u,&stage_hatena_set_state);
    }
    if (id==9) {
        space.bind(0x02111d8cu,&stage_castle_water_factory);
        space.bind(0x02112048u,&stage_metalnet_factory);
        space.bind(0x021121f0u,&stage_castle_flag_factory);
        space.bind(0x02111a30u,&stage_bird_factory);
    }
    if (id==85) {
        space.bind(0x0212f244u,&stage_sign_factory);
        space.bind(0x0212cc2cu,&stage_mip_factory);
        space.bind(0x0212bc78u,&stage_mip_set_state);
        space.bind(0x0212ed54u,&stage_jugem_factory);
        space.bind(0x0212e728u,&stage_jugem_set_state);
    }
    if (id==98) space.bind(0x0213b43cu,&stage_cannon_factory);
    if (id==100) {
        space.bind(0x0214589cu,&stage_door_factory);
        space.bind(0x02145370u,&stage_door_relative_player);
        space.bind(0x021453d8u,&stage_door_change_state);
        space.bind(0x02141ea4u,&stage_butterfly_factory);
        space.bind(0x02146d38u,&stage_fish_factory);
    }
    if (id!=2) return;
    space.bind(0x020b0580u,&stage_mushroom_factory);
    space.bind(0x020b0530u,&stage_mushroom_factory);
    space.bind(0x020b6ee8u,&stage_plain_actor_factory<0xdc,0x021094a0u>);
    space.bind(0x020bc3c8u,&stage_signpost_factory);
    space.bind(0x020e6c0cu,&stage_player_factory);
    space.bind(0x020e68f4u,&stage_player_construct);
    space.bind(0x020becc8u,&stage_player_body_model);
    space.bind(0x020e30a0u,&stage_player_change_state);
    space.bind(0x020c7dd0u,&stage_player_entrance_state);
    space.bind(0x020c6f34u,&stage_player_level_enter_cleanup);
    space.bind(0x020e308cu,&stage_player_is_state);
    space.bind(0x020c7ed4u,&stage_player_is_state_entering_level);
    space.bind(0x020c7e84u,&stage_player_is_entering_level);
    space.bind(0x020b36a0u,&stage_block_l_has_angle_x);
    space.bind(0x020fb8bcu,&stage_minimap_factory);
    space.bind(0x020f8808u,&stage_demo_factory);
    space.bind(0x020f7d74u,&stage_demo_dispatch_command);
    space.bind(0x020bd664u,&stage_player_dispatch_command);
    space.bind(0x020f26d4u,&stage_demo_bg_thunk);
    for (uint32_t factory : {0x020b406cu,0x020b409cu,0x020b40ccu,0x020b40fcu,0x020b412cu})
        space.bind(factory,&stage_block_l_factory);
    space.bind(0x020c9e40u,&stage_player_disable_interaction);
    space.bind(0x020b0f24u,&stage_plain_actor_factory<0x104,0x021086b4u>);
    space.bind(0x020ec32cu,&stage_tree_factory);
    space.bind(0x020f1b94u,&stage_plain_actor_factory<0xd8,0x0210b4c8u>);
}

void bind_stage_audio_runtime(packed::Space &space) {
    // Stage scenes only: the minigame runtime owns its own Sound boundary.
    space.bind(0x02051fb4u,&stage_snd_start_seq);
    space.bind(0x0204fa2cu,&stage_snd_stop_seq);
    space.bind(0x0204f8ccu,&stage_snd_move_volume);
    space.bind(0x0201320cu,&stage_sound_load_and_set_music_layer1);
    space.bind(0x02013050u,&stage_sound_stop_loaded_music_layer1);
}

void bind_stage_runtime(packed::Space &space) {
    space.bind(0x0201367cu,&NumStars);
    space.bind(0x0200e444u,&stage_camera_factory);
    space.bind(0x0200cb70u,&stage_camera_change_state);
    space.bind(0x02008550u,&stage_camera_dispatch_command);
    space.bind(0x0200d81cu,&stage_camera_player_reset);
    space.bind(0x0200ccacu,&stage_camera_set_pos);
    space.bind(0x0200ccc8u,&stage_camera_set_look_at);
    space.bind(0x0200d048u,&stage_camera_set_flag_3);
    space.bind(0x02029a68u,&stage_wipe_release);
    space.bind(0x02010ad8u,&stage_closest_player);
    space.bind(0x0200e768u,&stage_read_unaligned_short);
    space.bind(0x0200e748u,&stage_read_unaligned_int);
    space.bind(0x0200e8d4u,&stage_process_kuppa_script);
    space.bind(0x02011ffcu,&stage_sound_load_initial_group);
    space.bind(0x02029ee0u,&stage_event_get_bit);
    space.bind(0x02010be8u,&stage_drop_shadow_rad_height);
    space.bind(0x02015e64u,&stage_shadow_init_model);
    space.bind(0x0200cae4u,&stage_camera_state_init);
    space.bind(0x0203cbd8u,&stage_allocate);
    space.bind(0x0203cbe4u,&stage_allocate);
    space.bind(0x0203cbc0u,&stage_deallocate);
    space.bind(0x0203cbccu,&stage_deallocate);
    space.bind(0x02016958u,&stage_anim_model_ctor);
    space.bind(0x02015a50u,&stage_texture_sequence_ctor);
    space.bind(0x020163a0u,&stage_anim2_model_construct);
    space.bind(0x02016fd4u,&stage_model_set_file);
    space.bind(0x02015bf8u,&stage_frame_flags);
    space.bind(0x02015c0cu,&stage_frame_count);
    space.bind(0x02052820u,&pk_func_02052820);
    space.bind(0x02052800u,&pk_func_02052800);
    space.bind(0x0205283cu,&pk_func_0205283c);
    space.bind(0x0205a754u,&pk_mg_DecompressLZ16);
    space.bind(0x020527e8u,&stage_scale_matrix);
    space.bind(0x02015c20u,&stage_frame_set_animation);
    space.bind(0x02016748u,&stage_anim_model_set_animation);
    space.bind(0x0201597cu,&stage_texture_prepare);
    space.bind(0x02016abcu,&stage_model_polygon_id);
    space.bind(0x02015ef4u,&stage_shadow_set_file);
    space.bind(0x02015ebcu,&stage_shadow_init_cylinder);
    space.bind(0x02015ed8u,&stage_shadow_init_cuboid);
    space.bind(0x0203d384u,&stage_vector_lifetime);
    space.bind(0x020072c0u,&stage_vector_lifetime);
    space.bind(0x020148c8u,&stage_actor_cylinder_init);
    space.bind(0x02014878u,&stage_position_cylinder_construct);
    space.bind(0x020147d4u,&stage_position_cylinder_init);
    space.bind(0x02014820u,&stage_position_cylinder_pos);
    space.bind(0x02014818u,&stage_position_cylinder_owner);
    space.bind(0x0201490cu,&stage_centered_cylinder_init);
    space.bind(0x02037388u,&stage_actor_mesh_init);
    space.bind(0x02035840u,&stage_actor_mesh_start_water);
    space.bind(0x020356b4u,&stage_actor_mesh_limit);
    space.bind(0x02037570u,&stage_ground_construct);
    space.bind(0x020377b0u,&stage_line_construct);
    space.bind(0x02037764u,&stage_line_destroy);
    space.bind(0x02037670u,&stage_line_set);
    space.bind(0x02039cb8u,&stage_mbg_detect_ground);
    space.bind(0x020375d0u,&stage_line_position);
    space.bind(0x02037eecu,&stage_surface_construct);
    space.bind(0x02037ee8u,&stage_surface_destroy);
    space.bind(0x020397b8u,&stage_wall_facing);
    space.bind(0x02037534u,&stage_ground_destroy);
    space.bind(0x0203748cu,&stage_ground_set_owner_position);
    space.bind(0x02035354u,&stage_query_owned);
    space.bind(0x02037464u,&stage_ground_reset);
    space.bind(0x02037fecu,&stage_result_set_collider);
    space.bind(0x02037fd4u,&stage_result_record);
    space.bind(0x020393b4u,&stage_collider_owner);
    space.bind(0x020393acu,&stage_collider_id);
    space.bind(0x0203939cu,&stage_collider_radius);
    space.bind(0x0203938cu,&stage_collider_offset);
    space.bind(0x020397dcu,&stage_near_zero);
    space.bind(0x0203cfdcu,&stage_vector_distance);
    space.bind(0x0203cf40u,&stage_vector_horizontal_distance);
    space.bind(0x0203b7acu,&stage_vector_horizontal_angle);
    space.bind(0x0203b770u,&stage_vector_vertical_angle);
    space.bind(0x02053258u,&pk__ZN4cstd4fdivEii);
    space.bind(0x020396dcu,&stage_triangle_index);
    space.bind(0x01ffd8d8u,&stage_kcl_normal);
    space.bind(0x01ffd920u,&stage_kcl_surface);
    space.bind(0x02039488u,&stage_surface_pass);
    space.bind(0x0203549cu,&stage_query_water);
    space.bind(0x02035414u,&stage_query_stop_vanish);
    space.bind(0x02035428u,&stage_query_start_vanish);
    space.bind(0x02035468u,&stage_query_special);
    space.bind(0x02035644u,&stage_actor_wall_extent);
    space.bind(0x02073470u,&stage_array_new);
    space.bind(0x02017480u,&stage_wipe_construct);
    space.bind(0x02017450u,&stage_wipe_destroy);
    space.bind(0x020172c0u,&stage_wipe_load);
    space.bind(0x0203acccu,&stage_path_nodes);
    space.bind(0x0203ac80u,&stage_path_from_id);
    space.bind(0x0203acbcu,&stage_path_node_table);
    space.bind(0x0203acdcu,&stage_path_get_node);
    space.bind(0x0203ad34u,&stage_path_loops);
    space.bind(0x0203ad44u,&stage_path_pull_factor);
    space.bind(0x0203ad54u,&stage_path_unk004);
    space.bind(0x0203ad60u,&stage_path_num_nodes);
    space.bind(0x0203ad6cu,&stage_path_set_def);
    space.bind(0x0203ad74u,&stage_path_construct);
    space.bind(0x0203aca0u,&stage_path_defs);
    space.bind(0x02039760u,&stage_kcl_rebase);
    space.bind(0x020396f0u,&stage_kcl_set_file);
    space.bind(0x02039184u,&stage_collider_enable);
    space.bind(0x020393dcu,&stage_collider_is_enabled);
    space.bind(0x02039140u,&stage_collider_disable);
    space.bind(0x01ffb0ecu,&stage_kcl_origin_y);
    space.bind(0x01ffb0d0u,&stage_kcl_far_y);
    space.bind(0x0202a850u,&stage_vertical_bounds);
    space.bind(0x02013ee8u,&stage_spawn);
    space.bind(0x02010e2cu,&stage_spawn_actor);
    space.bind(0x020113c0u,&stage_actor_construct);
    space.bind(0x0201150cu,&stage_actor_construct);
    space.bind(0x02011268u,&stage_actor_before_init);
    space.bind(0x02043824u,&stage_mark_for_destruction);
    space.bind(0x0202e088u,&native_stage_factory);
    space.bind(0x02043decu,&native_fbase_construct);
    space.bind(0x02023204u,&native_particle_tracker_construct);
    space.bind(0x02016d58u,&native_simple_model_construct);
    space.bind(0x02039894u,&native_mesh_collider_construct);
    space.bind(0x020433b8u,&native_actor_post_construct);
    space.bind(0x02043bf8u,&stage_base_after_init);
    space.bind(0x02013ef4u,&stage_derived_after_init);
    space.bind(0x02011244u,&stage_actor_after_init);
    space.bind(0x0202e62cu,&stage_derived_after_init);
    space.bind(0x0204335cu,&stage_process_init);
    space.bind(0x020432e4u,&stage_process_cleanup);
    space.bind(0x02043288u,&stage_process_behavior);
    space.bind(0x0204322cu,&stage_process_render);
    space.bind(0x02043880u,&stage_reconcile_actor);
    space.bind(0x02044120u,&stage_actor_frame);
    space.bind(0x0202ddc8u,&stage_before_init);
    space.bind(0x02043c78u,&fbase_before_init);
    space.bind(0x0202e66cu,&scene_reset_faders_and_sound);
    space.bind(0x020553c0u,&stage_flush_fifo);
    space.bind(0x0205a588u,&stage_fill);
    space.bind(0x02020054u,&GetControllerMode);
    space.bind(0x0203d880u,&GetOwnerLanguage);
    space.bind(0x020182acu,&stage_load_file_at);
    space.bind(0x02018100u,&stage_load_compressed_at);
    space.bind(0x0201fcd4u,&stage_load_font);
    space.bind(0x0205a448u,&pk_MultiStore16);
    space.bind(0x0205a47cu,&pk_MultiStore_Int);
    space.bind(0x0205a490u,&pk_MultiCopy_Int);
}
}

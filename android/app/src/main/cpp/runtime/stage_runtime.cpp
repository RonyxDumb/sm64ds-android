#include "stage_runtime.h"
#include "actor_dispatch.h"
#include "collision_dispatch.h"
#include "boot_seams.h"
#include "packed_ptr.h"
#include "platform/arm9_services.h"
#include <cstring>
#include <exception>
#include <unordered_set>
#include <stdexcept>

extern "C" uint8_t GetControllerMode(int);
extern "C" int GetOwnerLanguage();
extern "C" void LoadFont(uint8_t);
extern "C" int LoadFileAt(int,void *);
extern "C" uint8_t data_0209d698;
extern "C" uint8_t data_0209d660;
extern "C" short data_0209d6d4;
extern "C" void pk_MultiStore16(unsigned short,sm64ds::packed::Ptr<char>,int);
extern "C" void pk_MultiStore_Int(int,sm64ds::packed::Ptr<int>,int);
extern "C" void pk_MultiCopy_Int(sm64ds::packed::Ptr<int>,sm64ds::packed::Ptr<int>,int);

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

void bind_stage_overlay_runtime(packed::Space &space,unsigned id) {
    if (id!=2) return;
    space.bind(0x020e6c0cu,&stage_player_factory);
    space.bind(0x020e68f4u,&stage_player_construct);
    space.bind(0x020becc8u,&stage_player_body_model);
    space.bind(0x020e30a0u,&stage_player_change_state);
    space.bind(0x020c7dd0u,&stage_player_entrance_state);
}

void bind_stage_runtime(packed::Space &space) {
    space.bind(0x0203cbd8u,&stage_allocate);
    space.bind(0x0203cbe4u,&stage_allocate);
    space.bind(0x0203cbc0u,&stage_deallocate);
    space.bind(0x0203cbccu,&stage_deallocate);
    space.bind(0x02016958u,&stage_anim_model_ctor);
    space.bind(0x020163a0u,&stage_anim2_model_construct);
    space.bind(0x02016fd4u,&stage_model_set_file);
    space.bind(0x02015bf8u,&stage_frame_flags);
    space.bind(0x02015c20u,&stage_frame_set_animation);
    space.bind(0x02016748u,&stage_anim_model_set_animation);
    space.bind(0x0201597cu,&stage_texture_prepare);
    space.bind(0x02016abcu,&stage_model_polygon_id);
    space.bind(0x02015ef4u,&stage_shadow_set_file);
    space.bind(0x02015ebcu,&stage_shadow_init_cylinder);
    space.bind(0x0203d384u,&stage_vector_lifetime);
    space.bind(0x020072c0u,&stage_vector_lifetime);
    space.bind(0x020148c8u,&stage_actor_cylinder_init);
    space.bind(0x02037388u,&stage_actor_mesh_init);
    space.bind(0x02035644u,&stage_actor_wall_extent);
    space.bind(0x02073470u,&stage_array_new);
    space.bind(0x02017480u,&stage_wipe_construct);
    space.bind(0x02017450u,&stage_wipe_destroy);
    space.bind(0x020172c0u,&stage_wipe_load);
    space.bind(0x0203acccu,&stage_path_nodes);
    space.bind(0x0203aca0u,&stage_path_defs);
    space.bind(0x02039760u,&stage_kcl_rebase);
    space.bind(0x020396f0u,&stage_kcl_set_file);
    space.bind(0x02039184u,&stage_collider_enable);
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

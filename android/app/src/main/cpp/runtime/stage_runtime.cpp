#include "stage_runtime.h"
#include "actor_dispatch.h"
#include "collision_dispatch.h"
#include "boot_seams.h"
#include "packed_ptr.h"
#include "platform/arm9_services.h"
#include "ntr/mmio.h"
#include "game_program.h"
#include "pc_audio/archive.h"
#include <cstring>
#include <array>
#include <map>
#include <exception>
#include <unordered_set>
#include <stdexcept>
#include <memory>
#include <type_traits>

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
// src/DecIfAbove0_Byte.c, linked once through the minigame manifest.
extern "C" unsigned int abi_DecIfAbove0_Byte(unsigned int);

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

int stage_before_init(uint32_t actor) {
    // The complete dScStage_c::BeforeInitResources body. The real scene
    // method must be bound separately; an unknown method never succeeds.
    return Ptr<bool(uint32_t)>(0x0202e66cu)(actor);
}

int fbase_before_init(uint32_t actor) {
    (void)packed::Space::current().data(actor,0x50);
    return true;
}

int scene_reset_faders_and_sound(uint32_t actor) {
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

void stage_update_message(uint32_t) {
    // Complete Stage::UpdateMessage (0x0201cb7c,
    // external/sm64ds-pc/src/_ZN5Stage13UpdateMessageEv.cpp; the unit's
    // local Message/SaveData bodies only stand in for the real members,
    // called here at their cartridge addresses). Every global is re-read
    // from the cartridge image, as the ROM reloads it after each call.
    auto &active=at<uint8_t>(0x0209d660u), &saving=at<uint8_t>(0x0209d654u);
    auto &countdown=at<uint8_t>(0x0209d67cu);
    const auto tick=[&countdown]() {
        if (countdown) {
            countdown=uint8_t(countdown-uint8_t(at<int32_t>(0x0208ee44u)));
            if (!countdown) at<uint8_t>(0x0209d670u)=1;
        }
    };
    if (!active) return;
    if (Ptr<int(void)>(0x0201bbc8u)()&0xff) { // Message::UpdateWindow
        if (!saving) { Ptr<void(void)>(0x0201c0b8u)(); return; } // Message::Update
        if (at<int16_t>(0x0209d6d4u)==0x295 && countdown==0x3c)
            Ptr<void(void)>(0x02013b9cu)(); // SaveData::SaveCurrentFile
        tick();
        return;
    }
    if (saving) {
        if (active) return;
        if (at<int16_t>(0x0209d6d4u)!=0x295) { saving=0; return; }
        Ptr<void(uint16_t)>(0x0201eb94u)(0x296); // Message::DisplaySaving
    }
}
void stage_oam_render_sub_palette(uint32_t data,int32_t x,int32_t y,int32_t palette,int32_t priority) {
    // Complete OAM::RenderSub (static, 0x0202199c,
    // external/sm64ds-pc/src/_ZN3OAM9RenderSubEP7OamAttriiii.cpp): the sub
    // engine's OAM::Render at unit scale, no rotation, default mode.
    Ptr<void(int,uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t)>(0x02020994u)(
        1,data,x,y,palette,priority,0x1000,0x1000,0,-1);
}
int stage_oam_load_affine_params(uint32_t oam,uint32_t count,uint32_t matrix) {
    // Complete OAM::LoadAffineParams (static, 0x02020884, recovered unit):
    // reuse a matching group of four attr3 coefficients (OamAttr is 8
    // bytes, attr3 at +6) or append one; -1 once 0x80 entries are used.
    uint32_t key[4];
    for (int i=0;i<4;++i) key[i]=uint16_t(at<uint32_t>(matrix+i*4)>>4);
    const int n=at<int32_t>(count);
    for (int i=0;i<n;i+=4) {
        const uint32_t e=oam+uint32_t(i)*8;
        if (key[0]==at<uint16_t>(e+6) && key[1]==at<uint16_t>(e+8+6) &&
            key[2]==at<uint16_t>(e+16+6) && key[3]==at<uint16_t>(e+24+6))
            return i>>2;
    }
    if (n>=0x80) return -1;
    const uint32_t e=oam+uint32_t(n)*8;
    for (int i=0;i<4;++i) at<uint16_t>(e+i*8+6)=uint16_t(at<int32_t>(matrix+i*4)>>4);
    at<int32_t>(count)=n+4;
    return n>>2;
}
int stage_scene_before_cleanup(uint32_t actor) {
    // Complete dScene_c::BeforeCleanupResources (0x0202e5f0,
    // src/game/stages/d_scene.cpp): fBase_c's check, then release the
    // looping-sound slot table (func_02011974(data_0209b53c)).
    if (!Ptr<int(uint32_t)>(0x02043bacu)(actor)) return 0;
    Ptr<void(uint32_t)>(0x02011974u)(0x0209b53cu);
    return 1;
}
void stage_scene_after_cleanup(uint32_t actor,uint32_t result) {
    // Complete dScene_c::AfterCleanupResources (0x0202e5d0): the native
    // scene-alive flag clears once the scene really went away, so
    // SpawnIfNecessary may spawn the requested next scene.
    if (result==2) data_02092660=0;
    Ptr<void(uint32_t,uint32_t)>(0x02043b2cu)(actor,result);
}
int scene_set_scene_to_spawn(uint32_t sceneID,uint32_t param) {
    // dScene_c::SetSceneToSpawn (static, 0x0202e36c): the pending scene
    // request is native-owned by GameProgram (data_02092664/data_0209f5b8),
    // so the stage calls the recovered member compiled into the runtime.
    return dScene_c::SetSceneToSpawn(sceneID,param);
}
void scene_start_scene_fade(uint32_t sceneID,uint32_t param,uint32_t color) {
    // dScene_c::StartSceneFade (static, 0x0202e348), same ownership.
    dScene_c::StartSceneFade(sceneID,param,uint16_t(color));
}
void scene_set_and_stop_color_fader() {
    // Complete dScene_c::SetAndStopColorFader (0x0202e1b4,
    // external/sm64ds-pc/src/_ZN8dScene_c20SetAndStopColorFaderEv.cpp) over
    // the native-owned shared color fader, as ResetFadersAndSound above.
    dScene_c::SetFaders(&data_0209f5e8);
    data_0209f5e8.speed = 0;
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

uint32_t native_model_base_construct(uint32_t model) {
    // Complete Model::Model() C2 (0x02016da8, recovered unit): the Model
    // vptr, transformsBuf = 0 and an identity mat4x3.
    auto &space=packed::Space::current();
    (void)space.data(model,0x50);
    at<uint32_t>(model)=0x0208e90cu;
    at<uint32_t>(model+0x4c)=0;
    std::memcpy(space.data(model+0x1c,48),space.data(0x02082128u,48),48);
    return model;
}

namespace {
uint32_t stage_allocate(uint32_t bytes) { return packed::Space::current().allocate(bytes); }
void stage_deallocate(uint32_t address) { if (address) packed::Space::current().release(address); }
// MSL func_02073470: preserve the cartridge's eight-byte cookie and use
// registered native callbacks for element construction and exception cleanup.
void stage_vec_ctor(uint32_t base,uint32_t count,uint32_t stride,uint32_t constructor,uint32_t destructor) {
    // MSL __cxa_vec_ctor (0x020733a8): construct in place; on an exception
    // destroy the built prefix in reverse (func_02073534) and rethrow, so
    // native diagnostics keep propagating unchanged.
    if (!constructor) return;
    uint32_t built=0;
    try {
        for (;built<count;++built) Ptr<void(uint32_t)>{constructor}(base+built*stride);
    } catch (...) {
        if (destructor) {
            try { while (built) Ptr<void(uint32_t)>{destructor}(base+(--built)*stride); }
            catch (...) { std::terminate(); }
        }
        throw;
    }
}
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
uint32_t stage_anim_model_base_ctor(uint32_t model) {
    // Complete ModelAnim::ModelAnim() C2 (0x02016998): Model C2, the
    // ModelAnim vptr, the Animation member and file = 0.
    (void)packed::Space::current().data(model,0x64);
    native_model_base_construct(model);
    at<uint32_t>(model)=0x0208e980u;
    stage_frame_construct(model+0x50,0x0208e9a4u);
    at<uint32_t>(model+0x60)=0;
    return model;
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
int stage_query_owned(uint32_t query,uint32_t owner) {
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
int stage_wall_facing(int normal) { return normal<0x600 && normal>-0xccc; }
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
int stage_near_zero(int value) { return value>=-8 && value<=8; }
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
int32_t stage_vector_horizontal_angle(uint32_t a,uint32_t b) {
    const int32_t x=static_cast<int32_t>(at<uint32_t>(b)-at<uint32_t>(a));
    const int32_t z=static_cast<int32_t>(at<uint32_t>(b+8)-at<uint32_t>(a+8));
    return cstd::atan2(x,z);
}
int32_t stage_vector_vertical_angle(uint32_t a,uint32_t b) {
    const int32_t y=static_cast<int32_t>(at<uint32_t>(a+4)-at<uint32_t>(b+4));
    return cstd::atan2(y,stage_vector_horizontal_distance(a,b));
}
int32_t stage_triangle_index(uint32_t collider,uint32_t triangle) {
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
int stage_surface_pass(uint32_t,uint32_t surface,uint32_t query,bool check14) {
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
int stage_cutscene_object_render(uint32_t self) {
    // Complete CutsceneObject::Render (ov002 0x020f80ac; recovered isolated
    // unit): mModel 0xdc, mModelAnim 0xe0, mOpacity 0x102, mScale 0x80.
    const auto draw=[](uint32_t model,uint32_t arg) {
        Ptr<void(uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(model)+5*4))(model,arg);
    };
    if (at<uint32_t>(self+8)==0x19) {
        const uint32_t model=at<uint32_t>(self+0xdc),mat=model+0x1c;
        std::memcpy(packed::Space::current().data(mat,0x30),packed::Space::current().data(0x0209b41cu,0x30),0x30);
        for (unsigned i=0;i<3;++i) {
            for (unsigned lane=0;lane<3;++lane)
                at<int32_t>(mat+0x24+lane*4)=at<int32_t>(0x0210bb7cu+(i*3+lane)*4);
            draw(model,0);
        }
        return 1;
    }
    const uint8_t opacity=at<uint8_t>(self+0x102);
    if (!opacity) return 1;
    if (const uint32_t model=at<uint32_t>(self+0xdc)) {
        Ptr<void(uint32_t,uint32_t,int)>(0x02016a9cu)(model,opacity,0);
        draw(model,self+0x80);
    } else if (const uint32_t anim=at<uint32_t>(self+0xe0)) {
        Ptr<void(uint32_t)>(0x020f65b8u)(anim);
        Ptr<void(uint32_t,uint32_t,int)>(0x02016a9cu)(anim,at<uint8_t>(self+0x102),0);
        draw(anim,self+0x80);
    }
    return 1;
}
int stage_jugem_render(uint32_t self) {
    // Complete LakituBro::Render (ov085 0x0212ead0): unless hidden
    // (unk_2dc == 1), advance the texture sequence (0x1d8) over
    // mModelAnim1's components (0x118) and draw it (vtable slot 5).
    if (at<uint8_t>(self+0x2dc)==1) return 1;
    Ptr<int(uint32_t,uint32_t)>(0x02015988u)(self+0x1d8,self+0x118);
    const uint32_t model=self+0x110;
    Ptr<int(uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(model)+5*4))(model,0);
    return 1;
}
int stage_door_behavior(uint32_t self) {
    // Complete Door::Behavior (ov100 0x02145550): the player-relative test,
    // then the callback node's PMF (mCallbackNode 0x140, PMF at node+8).
    auto &space=packed::Space::current();
    const int res=Ptr<int(uint32_t)>(0x02145370u)(self);
    const uint32_t node=at<uint32_t>(self+0x140);
    if (at<uint32_t>(node+8)) {
        packed::MemberCall member{};
        std::memcpy(&member,space.data(node+8,8),8);
        const auto target=space.resolve_member(self,member);
        using Callback=void (*)(uint32_t,int);
        Callback callback=nullptr;
        std::memcpy(&callback,&target.code,sizeof callback);
        callback(target.object,res);
    }
    return 1;
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
void stage_cc_set_pos_relative(uint32_t self,uint32_t offset) {
    // Complete dCcAcPos_c::SetPosRelativeToActor(const Vector3 &): owner at
    // 0x30 (dCcAc_c.h), pos at 0x34; actor mPos 0x5c, mAngleY 0x8e.
    const uint32_t actor=at<uint32_t>(self+0x30);
    Ptr<void(uint32_t,uint32_t,int,uint32_t)>(0x0203b6a4u)(self+0x34,actor+0x5c,at<int16_t>(actor+0x8e),offset);
}
int stage_mbg_detect_line(uint32_t self,uint32_t ray) {
    // Complete dBgW_KcMbg::DetectClsn(dBgCh_Lin &) (recovered isolated
    // unit): the line in mesh-local space through the shared query at
    // 0x020a0d0c (start 0x38, end 0x44, hasClsn 0x50, lineEnd 0x54,
    // clsnDist 0x60; dBgPi base at 0x10).
    auto &space=packed::Space::current();
    int32_t start[3],end[3],world[3];
    using Map=void(uint32_t,uint32_t,uint32_t);
    Ptr<Map>(0x02039e48u)(self,ray+0x38,space.address(start));
    Ptr<Map>(0x02039e48u)(self,ray+0x54,space.address(end));
    const uint8_t hadClsn=at<uint8_t>(ray+0x50);
    const uint32_t line=0x020a0d0cu;
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(line,space.address(start),space.address(end),0);
    if (hadClsn) at<uint8_t>(line+0x50)=1;
    Ptr<void(uint32_t,uint32_t)>(0x02035394u)(line,ray);
    const int hit=Ptr<int(uint32_t,uint32_t)>(0x01ffb0fcu)(self,line);
    if (hit) {
        const int32_t distance=at<int32_t>(line+0x60);
        Ptr<Map>(0x02039e30u)(self,0x020a0d60u,space.address(world));
        Ptr<void(uint32_t,uint32_t)>(0x020375ecu)(ray,space.address(world));
        at<int32_t>(ray+0x60)=distance;
        std::memcpy(space.data(ray+0x14,0x24),space.data(0x020a0d20u,0x24),0x24);
        at<uint8_t>(ray+0x50)=1;
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
int stage_block_l_has_angle_x(uint32_t actor) {
    // Complete daObjBlockL_c::HasNonzeroAngleX (0x020b36a0).
    return at<int16_t>(actor+0x8c)!=0;
}
int32_t stage_event_get_bit(uint32_t bit) {
    // Complete Event::GetBit (0x02029ee0).
    return at<int32_t>(0x0209f34cu)&int32_t(1u<<bit);
}
int stage_player_is_state(uint32_t player,uint32_t state) {
    // Complete Player::IsState (0x020e308c): pointer identity of mState.
    return at<uint32_t>(player+0x370)==state;
}
int stage_bg_actor_update_kill_by_mega_char(uint32_t self,int32_t a,int32_t b,int32_t c,int32_t d) {
    // Complete dBgActor_c::UpdateKillByMegaChar(s16, s16, s16, Fix12i)
    // (external/sm64ds-pc/src/actors/dBgActor_c.cpp, frame ov002 0x020ee674):
    // while being thrown (+0x31c), cast a line 0xc8000 ahead along
    // mPrevAngleY and turn around on a hit, spin by (a, b, c), move, count
    // the +0x31d timer down to Kill() (vtable slot 31), then func_ov002_020ee5d0.
    if (!at<uint8_t>(self+0x31c)) return 0;
    auto &space=packed::Space::current();
    const uint32_t work=space.allocate(0x9c),vin=work,vmid=work+0xc,vout=work+0x18,ray=work+0x24;
    at<int32_t>(vin)=0; at<int32_t>(vin+4)=0; at<int32_t>(vin+8)=0xc8000;
    at<int32_t>(vmid)=0; at<int32_t>(vmid+4)=0; at<int32_t>(vmid+8)=0;
    Ptr<void(uint32_t,int)>(0x0203bd6cu)(0x020a0e68u,at<int16_t>(self+0x94)); // Matrix4x3_FromRotationY
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02052858u)(vin,0x020a0e68u,vmid);  // MulVec3Mat4x3
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203d340u)(vout,self+0x5c,vmid);  // Vec3_Add
    std::memcpy(space.data(vmid,12),space.data(vout,12),12);
    Ptr<void(uint32_t)>(0x020377b0u)(ray);                                     // dBgCh_Lin C1
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(ray,self+0x5c,vmid,self);
    if (Ptr<int(uint32_t)>(0x02038638u)(ray)) at<int16_t>(self+0x94)=int16_t(at<int16_t>(self+0x94)+0x8000);
    at<int16_t>(self+0x8c)=int16_t(at<int16_t>(self+0x8c)+a);
    at<int16_t>(self+0x8e)=int16_t(at<int16_t>(self+0x8e)+b);
    at<int16_t>(self+0x90)=int16_t(at<int16_t>(self+0x90)+c);
    Ptr<void(uint32_t,uint32_t)>(0x02010c30u)(self,0);                          // UpdatePos(0)
    if (Ptr<int(uint32_t)>(0x0203add4u)(self+0x31d)==0)                         // DecIfAbove0_Byte
        Ptr<void(uint32_t)>(at<uint32_t>(at<uint32_t>(self)+0x7c))(self);       // Kill()
    Ptr<void(uint32_t,int32_t)>(0x020ee5d0u)(self,d);
    Ptr<void(uint32_t)>(0x02037764u)(ray);                                     // dBgCh_Lin D1
    space.release(work);
    return 1;
}
int stage_ground_height_below(uint32_t actor) {
    // Complete func_ov102_02149610 (external/sm64ds-pc/src/func_ov102_02149610.cpp,
    // frame ov102 0x02149610): a dBgCh_Gnd from 0x28000 above the actor with
    // probe height 0x3e8000; the hit floor height, else the actor's own y.
    auto &space=packed::Space::current();
    const uint32_t work=space.allocate(0x60),pos=work,ground=work+0xc;
    at<int32_t>(pos)=at<int32_t>(actor+0x5c);
    at<int32_t>(pos+4)=at<int32_t>(actor+0x60)+0x28000;
    at<int32_t>(pos+8)=at<int32_t>(actor+0x64);
    Ptr<void(uint32_t)>(0x02037570u)(ground);                                // dBgCh_Gnd C1
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x0203748cu)(ground,pos,0);       // SetObjAndPos
    at<int32_t>(ground+0x4c)=0x3e8000;                                        // mProbeHeight
    int32_t height=at<int32_t>(actor+0x60);
    if (Ptr<int(uint32_t)>(0x02038f44u)(ground)) height=at<int32_t>(ground+0x44); // clsnY
    Ptr<void(uint32_t)>(0x02037534u)(ground);                                // dBgCh_Gnd D1
    space.release(work);
    return height;
}
int stage_camera_run_behavior(uint32_t self) {
    // Complete func_0200ca50 (external/sm64ds-pc/src/func_0200ca50.cpp):
    // approach the view's target roll (+0x17a towards view(+0x13c)+0x24, step
    // 0x80); unless flag 3 is set, either the 0x8000 path (func_0200cbe0) or
    // the current camera behavior's PMF (+0x138 record, +8) on this camera.
    auto &space=packed::Space::current();
    const uint32_t view=at<uint32_t>(self+0x13c);
    Ptr<int(uint32_t,int,int)>(0x0203aed8u)(self+0x17a,at<int16_t>(view+0x24),0x80);
    int result=1;
    const uint32_t flags=at<uint32_t>(self+0x154);
    if (flags&8) {
    } else if (flags&0x8000) {
        Ptr<int(uint32_t)>(0x0200cbe0u)(self);
    } else {
        const uint32_t behavior=at<uint32_t>(self+0x138);
        if (at<uint32_t>(behavior+8)) {
            packed::MemberCall member{};
            std::memcpy(&member,space.data(behavior+8,8),8);
            const auto target=space.resolve_member(self,member);
            using Callback=int (*)(uint32_t);
            Callback callback=nullptr;
            std::memcpy(&callback,&target.code,sizeof callback);
            result=callback(target.object);
        }
    }
    return result;
}
int stage_actor_forward_line_hit(uint32_t,uint32_t actor) {
    // Complete func_ov002_020ef070 (external/sm64ds-pc/src/func_ov002_020ef070.cpp,
    // frame read from ov002 0x020ef070): a dBgCh_Lin from 0x3c000 above the
    // actor along its yaw (0x64 units, 0x82 for param1 2). On a hit, actor
    // 0xbf runs func_ov002_020d8838; the hit dBgPi is copied (vtable
    // data_02099368) and the owner's vtable slot 23 is called with the actor.
    auto &space=packed::Space::current();
    const uint32_t line=space.allocate(0x78),work=space.allocate(0x40);
    const uint32_t v1=work,v2=work+0xc,hit=work+0x18;
    Ptr<void(uint32_t)>(0x020377b0u)(line); // dBgCh_Lin C1
    const int32_t x=at<int32_t>(actor+0x5c),y=at<int32_t>(actor+0x60)+0x3c000,z=at<int32_t>(actor+0x64);
    const int32_t scale=at<int32_t>(actor+8)==2 ? 0x82 : 0x64;
    const uint32_t angle=uint32_t(at<uint16_t>(actor+0x8e))>>4;
    at<int32_t>(v1)=x; at<int32_t>(v1+4)=y; at<int32_t>(v1+8)=z;
    at<int32_t>(v2)=scale*at<int16_t>(0x02082214u+angle*4)+x;
    at<int32_t>(v2+4)=y;
    at<int32_t>(v2+8)=scale*at<int16_t>(0x02082214u+angle*4+2)+z;
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(line,v1,v2,actor); // SetObjAndLine
    int result=0;
    if (Ptr<int(uint32_t)>(0x02038638u)(line)) {                                   // DetectClsn
        if (at<uint16_t>(actor+0xc)==0xbf) Ptr<void(uint32_t)>(0x020d8838u)(actor);
        std::memcpy(space.data(hit+4,0x24),space.data(line+0x14,0x24),0x24);         // the dBgPi fields
        at<uint32_t>(hit)=0x02099368u;                                               // dBgPi vtable
        const auto clsnID=[hit] { return Ptr<uint32_t(uint32_t)>(0x02037f4cu)(hit); };
        if (clsnID()!=0xffffffffu) {
            if (const uint32_t owner=Ptr<uint32_t(uint32_t)>(0x02010f3cu)(clsnID())) {
                Ptr<void(uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(owner)+0x5c))(owner,actor);
                result=1;
            }
        }
        Ptr<void(uint32_t)>(0x02038144u)(hit); // dBgPi D1
    }
    Ptr<void(uint32_t)>(0x02037764u)(line); // dBgCh_Lin D1
    space.release(work);
    space.release(line);
    return result;
}
int stage_player_vertical_line_probe(uint32_t self,int32_t y0,int32_t y1,uint32_t outY) {
    // Complete func_ov002_020c607c (external/sm64ds-pc/src/actors/Player.cpp,
    // frame read from ov002 0x020c607c): a dBgCh_Lin from (x, y0, z) to
    // (x, y1, z); the hit height goes to *outY (0x80000000 when nothing is
    // hit), the hit owner to Player+0x36c; 1/2 for owners with mFlags bit 24/25.
    auto &space=packed::Space::current();
    at<uint32_t>(self+0x36c)=0;
    at<uint32_t>(outY)=0x80000000u;
    const uint32_t rc=space.allocate(0x78),line=space.allocate(0x24);
    int result=0;
    Ptr<void(uint32_t)>(0x020377b0u)(rc); // dBgCh_Lin C1
    const uint32_t a=line,b=line+0xc,pos=line+0x18;
    at<int32_t>(a)=at<int32_t>(self+0x5c); at<int32_t>(a+4)=y0; at<int32_t>(a+8)=at<int32_t>(self+0x64);
    at<int32_t>(b)=at<int32_t>(self+0x5c); at<int32_t>(b+4)=y1; at<int32_t>(b+8)=at<int32_t>(self+0x64);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x02037670u)(rc,a,b,self); // SetObjAndLine
    if (Ptr<int(uint32_t)>(0x02038638u)(rc)) {                                 // DetectClsn
        Ptr<void(uint32_t,uint32_t)>(0x020375d0u)(pos,rc);                     // GetClsnPos
        at<int32_t>(outY)=at<int32_t>(pos+4);
        const auto clsnID=[rc] { return Ptr<int(uint32_t)>(0x02037f4cu)(rc+0x10); }; // dBgPi::GetClsnID
        if (clsnID()!=-1) {
            const uint32_t found=Ptr<uint32_t(uint32_t)>(0x02010f3cu)(uint32_t(clsnID()));
            at<uint32_t>(self+0x36c)=found;
            if (found) {
                const uint32_t flags=at<uint32_t>(found+0xb0);
                if (flags&0x1000000u) result=1;
                else if (flags&0x2000000u) result=2;
            }
        }
    }
    Ptr<void(uint32_t)>(0x02037764u)(rc); // dBgCh_Lin D1
    space.release(line);
    space.release(rc);
    return result;
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
uint32_t stage_meter_factory() {
    // Complete dMeter_c_classInit (ov002 0x020fe154, the HUD actor 334):
    // operator new(0x7c), fBase_c C2, the dBase_c vtable, then 0x0210c2c8.
    const uint32_t meter=packed::Space::current().allocate(0x7c);
    native_fbase_construct(meter);
    at<uint32_t>(meter)=0x0208e4b8u;
    at<uint32_t>(meter)=0x0210c2c8u;
    return meter;
}
int32_t stage_read_unaligned_short(uint32_t p) {
    return int16_t(at<uint8_t>(p)|(at<uint8_t>(p+1)<<8));
}
uint32_t stage_read_unaligned_int(uint32_t p) {
    return at<uint8_t>(p)|(at<uint8_t>(p+1)<<8)|(at<uint8_t>(p+2)<<16)|(uint32_t(at<uint8_t>(p+3))<<24);
}
// Original virtual dispatch: the DS vtable word at slot*4, receiver in r0.
template<class R,class... A> R stage_virtual(uint32_t object,unsigned slot,A... args) {
    return Ptr<R(uint32_t,A...)>(at<uint32_t>(at<uint32_t>(object)+slot*4))(object,args...);
}
// A DS-resident fader (the Kuppa script's dFdColor_c at data_0209b294)
// handed to the scene fader slot. dScene_c::SetFaders keeps native fader
// objects (the DS word data_0209f5bc is a proxy forwarding to them), so the
// DS object is driven through its own cartridge vtable, and its interpolator
// fields are mirrored after every call for native readers.
struct StageDsFader final : dFdBrightness_c {
    packed::Space *space;
    uint32_t object;
    StageDsFader(packed::Space &s,uint32_t o) : space(&s), object(o) { sync(); }
    void sync() {
        packed::Space::Scope scope(*space);
        currInterp=at<int32_t>(object+4);
        speed=at<int32_t>(object+8);
    }
    template<class R,class... A> R call(unsigned slot,A... args) {
        packed::Space::Scope scope(*space);
        if constexpr (std::is_void_v<R>) {
            stage_virtual<void,A...>(object,slot,args...);
            sync();
        } else {
            const R result=stage_virtual<R,A...>(object,slot,args...);
            sync();
            return result;
        }
    }
    void AdvanceFade() override { call<void>(2); }
    int SetBackwardTime(u32 frames) override { return call<int,uint32_t>(3,frames); }
    int SetForwardTime(u32 frames) override { return call<int,uint32_t>(4,frames); }
    int IsAtStart() override { return call<int>(5); }
    int IsAtEnd() override { return call<int>(6); }
    int IsBetweenStartAndEnd() override { return call<int>(7); }
    void SetToEnd() override { call<void>(8); }
    void SetToStart() override { call<void>(9); }
};
void scene_set_faders(uint32_t fader) {
    // Complete dScene_c::SetFaders(FaderBrightness *) (0x0202e1e0,
    // external/sm64ds-pc/src/_ZN8dScene_c9SetFadersEP15FaderBrightness.cpp)
    // for a DS fader object, through the native scene fader slot. Adapters
    // stay alive for the session: the native slot may still name one.
    static std::vector<std::unique_ptr<StageDsFader>> adapters;
    auto &space=packed::Space::current();
    StageDsFader *adapter=nullptr;
    for (auto &a : adapters) if (a->object==fader && a->space==&space) adapter=a.get();
    if (!adapter) {
        adapters.push_back(std::make_unique<StageDsFader>(space,fader));
        adapter=adapters.back().get();
    }
    dScene_c::SetFaders(adapter);
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
    if (std::getenv("SM64DS_TRACE_CMDS")) { std::fprintf(stderr,"CMD tbl=%08x p=%08x idx=%u:",table,p,index); for(int i=0;i<16;++i) std::fprintf(stderr," %02x",at<uint8_t>(p+i)); std::fprintf(stderr,"\n"); }
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
extern "C" void abi___sinit_02075154();
extern "C" void abi_SetBg3Offset(unsigned int,unsigned int); // minigame manifest unit
int32_t stage_cstd_sqrt(uint64_t value) {
    // cstd::sqrt(u64) through the same native hardware-root model
    // (native_math.cpp) the title and collision code already use.
    return cstd::sqrt(value);
}
int32_t stage_aeabi_idiv(int32_t a,int32_t b) {
    // __aeabi_idiv / _s32_div_f (ITCM 0x01ffabe4): the quotient in r0, the
    // only result the recovered C callers read. Same semantics as the
    // frontend's pk___aeabi_idiv (the frontend binds its own copy).
    if (!b) throw std::runtime_error("Native integer division by zero.");
    return a==INT32_MIN && b==-1 ? INT32_MIN : a/b;
}
// ITCM soft-float runtime (hand-assembly primitives, read from the cartridge
// code). Floats travel as their IEEE-754 bit patterns in core registers.
float stage_float_bits(uint32_t bits) { float f; std::memcpy(&f,&bits,4); return f; }
int32_t stage_float_order(uint32_t bits) {
    // The ROM's ordering trick: clear the sign and negate negatives, so
    // -0 == +0 and the integers compare like the floats.
    return int32_t(bits)<0 ? int32_t(0u-(bits&0x7fffffffu)) : int32_t(bits);
}
bool stage_float_nan(uint32_t a,uint32_t b) { return (a<<1)>0xff000000u || (b<<1)>0xff000000u; }
int stage_float_less(uint32_t a,uint32_t b) {
    // func_01ff98f4: a < b; unordered operands answer 0.
    return !stage_float_nan(a,b) && stage_float_order(a)<stage_float_order(b);
}
int stage_float_greater(uint32_t a,uint32_t b) {
    // func_01ff99a4: a > b; unordered operands answer 0.
    return !stage_float_nan(a,b) && stage_float_order(a)>stage_float_order(b);
}
int32_t stage_float_to_int(uint32_t bits) {
    // func_01ffa344: truncate toward zero; NaN 0, out of range saturates.
    const float f=stage_float_bits(bits);
    if (f!=f) return 0;
    if (f>=2147483648.0f) return INT32_MAX;
    if (f<-2147483648.0f) return INT32_MIN;
    return int32_t(f);
}
uint32_t stage_int_to_float(int32_t value) {
    // func_01ffa4bc: round-to-nearest-even, the runtime's default mode.
    const float f=float(value);
    uint32_t bits; std::memcpy(&bits,&f,4);
    return bits;
}
int32_t stage_cstd_atan2(int32_t y,int32_t x) {
    // cstd::atan2 (src/cstd_atan2.cpp, linked natively as runtime/input_atan2).
    return cstd::atan2(y,x);
}
void stage_load_fog(uint32_t self) {
    // Complete Stage::LoadFog (external/sm64ds-pc/src/_ZN5Stage7LoadFogEv.cpp)
    // with Stage.h/Fog.h offsets: mFog[2] at 0x96c, 0x28-byte records.
    const uint32_t fog=self+0x96c;
    uint8_t a=0,b=0x28;
    for (unsigned i=0;i<0x20;++i) {
        at<uint8_t>(fog+i)=a; at<uint8_t>(fog+0x28+i)=b;
        a=uint8_t(a+4); b=uint8_t(b+2);
    }
    at<uint8_t>(fog+0x20)=0;
    at<uint8_t>(fog+0x28+0x20)=1;
    at<uint8_t>(fog+0x28+0x21)=6;
    at<uint16_t>(fog+0x28+0x22)=0;
    at<uint16_t>(fog+0x28+0x24)=0x6000;
    const int count=at<int32_t>(0x0209f258u);
    if (count==0) return;
    uint32_t src=at<uint32_t>(0x0209f31cu),dst=fog;
    for (int j=0;j<count;++j,src+=8,dst+=0x28) {
        if (at<uint8_t>(src)!=1) { at<uint8_t>(dst+0x20)=0; continue; }
        const uint16_t color=uint16_t((at<uint8_t>(src+1)>>3)|((at<uint8_t>(src+2)>>3)<<5)|
                                      ((at<uint8_t>(src+3)>>3)<<10));
        Ptr<void(uint32_t,uint32_t,int32_t,int32_t)>(0x0202c830u)(dst,color,
            at<uint16_t>(src+4),at<uint16_t>(src+6));
    }
}
uint32_t stage_particle_alloc_tex_vram(uint32_t size,uint32_t isTexel4x4) {
    // Complete Particle::Texture::AllocTexVram (recovered isolated unit).
    if (isTexel4x4&0xff) {
        const uint32_t address=at<uint32_t>(0x0209ee88u);
        at<uint32_t>(0x0209ee88u)=address+size;
        return address;
    }
    return Ptr<uint32_t(uint32_t)>(0x02045c60u)(size); // Model::GetVramOffset
}
uint32_t stage_particle_alloc_pal_vram(uint32_t size,uint32_t fromLowAddress) {
    // Complete Particle::Texture::AllocPalVram (recovered isolated unit).
    if (fromLowAddress&0xff) {
        const uint32_t aligned=(size+7)&~7u,address=at<uint32_t>(0x0209ee84u);
        at<uint32_t>(0x0209ee84u)=address+aligned;
        return address;
    }
    const uint32_t aligned=(size+15)&~15u,address=at<uint32_t>(0x0209ee8cu)-aligned;
    at<uint32_t>(0x0209ee8cu)=address;
    return address;
}
void stage_shadow_clean_all() {
    // Complete static ShadowModel::CleanAll (recovered isolated unit):
    // unlink the list at data_0209cef4 (prev 0x20, next 0x24).
    while (const uint32_t head=at<uint32_t>(0x0209cef4u)) {
        const uint32_t next=at<uint32_t>(head+0x24);
        if (next) at<uint32_t>(next+0x20)=0;
        at<uint32_t>(head+0x24)=0;
        at<uint32_t>(0x0209cef4u)=next;
    }
    at<uint8_t>(0x0209ceecu)=0;
}
void stage_particle_tracker_initialise(uint32_t self) {
    // Complete Particle::SysTracker::Initialise (recovered isolated unit).
    // Callbacks are passed as their original code handles.
    const int8_t level=at<int8_t>(0x0209f2f8u);
    uint32_t allocSize=0x8c00; int countA=0x28,countB=0x100;
    if (level==0x24 || level==0x26 || level==0x28) { countA=0x40; countB=0x140; allocSize=0xa800; }
    const uint32_t base=Ptr<uint32_t(uint32_t)>(0x0203cbd8u)(allocSize);
    at<uint32_t>(0x0209ee80u)=base;
    at<uint32_t>(0x0209ee78u)=base;
    at<uint32_t>(0x0209ee7cu)=base+allocSize;
    at<int32_t>(0x0209ee88u)=Ptr<int32_t()>(0x02045d10u)();
    at<int32_t>(0x0209ee84u)=Ptr<int32_t()>(0x02045cf0u)();
    at<int32_t>(0x0209ee8cu)=Ptr<int32_t()>(0x02045ce0u)();
    const uint32_t manager=Ptr<uint32_t(uint32_t,int32_t,int32_t,int32_t,uint32_t)>(0x0204a4c8u)(
        0x02023178u,countA,countB,0x1a,0x3e);
    at<uint32_t>(self+4)=manager;
    at<int32_t>(manager+0x30)=0x8000;
    constexpr uint32_t embedded=0x02075f14u;
    if (Ptr<int32_t(uint32_t,uint32_t,uint32_t)>(0x0206e28cu)(embedded,0x0208f668u,4)!=0) {
        at<uint32_t>(self)=embedded;
    } else {
        const uint32_t size=at<uint32_t>(embedded+4)>>8;
        const uint32_t dst=Ptr<uint32_t(uint32_t)>(0x0203c1c4u)(size);
        Ptr<void(uint32_t,uint32_t)>(0x0205a754u)(embedded+4,dst);
        Ptr<void(uint32_t,uint32_t)>(0x02058c24u)(dst,size);
        at<uint32_t>(self)=dst;
    }
    Ptr<void(uint32_t,uint32_t)>(0x0204a17cu)(at<uint32_t>(self+4),at<uint32_t>(self));
    Ptr<int32_t(uint32_t,uint32_t)>(0x0204a0dcu)(at<uint32_t>(self+4),0x02023134u);
    Ptr<int32_t(uint32_t,uint32_t)>(0x0204a028u)(at<uint32_t>(self+4),0x020230ecu);
    const uint32_t resource=at<uint32_t>(self);
    if (resource!=embedded) {
        const uint32_t heap=at<uint32_t>(0x020a0ea0u);
        const uint32_t oldSize=at<uint32_t>(resource+0x18);
        Ptr<uint32_t(uint32_t,uint32_t)>(0x0203c274u)(heap,resource);
        Ptr<uint32_t(uint32_t,uint32_t,uint32_t)>(0x0203c578u)(heap,resource,oldSize);
    }
}
int stage_behavior(uint32_t self) {
    // Complete Stage::Behavior (0x0202bbbc; external/sm64ds-pc/src/
    // _ZN5Stage8BehaviorEv.cpp, src/game/stages/d_s_stage_methods.cpp).
    // Adaptation: the original CheckCameraInput/CheckInput bodies already ran
    // once for this frame in StageFlow::update_input (Android input bridge),
    // and the fader query uses GameProgram's native fader (data_0209f5bc).
    (void)self;
    auto u8at=[](uint32_t a) -> uint8_t & { return at<uint8_t>(a); };
    auto call=[](uint32_t a) { Ptr<void()> fn(a); fn(); };
    auto sound=[](int id) { Ptr<void(int)>(0x02012790u)(id); };
    at<uint32_t>(0x0209b464u)=at<uint32_t>(0x0209b454u);
    if (at<uint16_t>(0x0209f300u)) at<uint16_t>(0x0209f300u)-=1;
    if (u8at(0x0209f2c4u)==2 && u8at(0x0209f22cu)==0) call(0x02026290u); // PS_Cleanup
    call(0x0200e8d4u); // ProcessKuppaScript
    if (!data_0209f5bc->IsAtStart()) { call(0x02015e14u); return 1; } // ShadowModel::CleanAll
    if (u8at(0x0209f2d8u)!=1) {
        call(0x0201cb7cu); // UpdateMessage
    } else {
        const uint8_t c9c=u8at(0x0209fc9cu);
        if (c9c) {
            call(0x02032f54u);
            if (u8at(0x0209fcc8u)>=7u) call(0x020199a4u);
            return 1;
        }
        if (at<int32_t>(0x0209fc68u)==6) {
            call(0x02019a58u); sound(0x123); call(0x02011c24u); call(0x02030a78u);
            Ptr<void(int)>(0x020345b0u)(0x13);
            return 1;
        }
        int cnt=0;
        for (unsigned i=0;i<4;++i)
            if (const uint32_t p=at<uint32_t>(0x0209f394u+i*4)) cnt+=at<uint8_t>(p+0x711)!=0;
        if (cnt>=u8at(0x0209fc50u)) {
            auto &timer=at<uint16_t>(0x0209f304u);
            auto &count=u8at(0x0209f2bcu);
            if (timer==0x28 && count==3) sound(0x2b);
            if (u8at(0x0209f2c4u)==0 && (timer!=0 || count!=0)) {
                timer=uint16_t(timer-at<int32_t>(0x0208ee44u));
                if (timer==0 && count!=0) {
                    count-=1; timer=0x28;
                    if (count) sound(0x2b);
                    else { sound(0x2a); Ptr<void(int)>(0x0201320cu)(0x4d); }
                }
            }
        }
        if (at<uint16_t>(0x02111188u)==0 && u8at(0x0209f204u)) {
            if (at<int32_t>(0x0209fc68u)==0) {
                if (at<int8_t>(0x02092110u)<0 && at<int32_t>(0x0209d4b0u)==0) {
                    if (u8at(0x0209f294u)==0 && u8at(0x0209f290u)==0) call(0x02023a80u);
                    else call(0x0202376cu);
                }
            } else {
                Ptr<void(int,int,int)>(0x0202e348u)(7,0,0);
                u8at(0x02092778u)=1;
                at<int32_t>(0x0209d4b0u)=0;
            }
            Ptr<void(int)>(0x02013050u)(0x3c);
        }
    }
    const auto level=[](int sublevel) { return Ptr<int(int)>(0x02013558u)(sublevel); };
    const int8_t next=at<int8_t>(0x02092110u),current=at<int8_t>(0x0209f2f8u);
    if (next>=0) {
        const int lvl=level(next),lvl2=level(current);
        if (u8at(0x0209f2d8u)!=2 && lvl<=0xe && lvl!=lvl2 && at<int8_t>(0x02092118u)<0 &&
            (next!=0xc || u8at(0x0209f268u)!=4))
            Ptr<void(int,int)>(0x0202e36cu)(4,0);
        else
            Ptr<void(int,int)>(0x0202e36cu)(3,0);
        const auto inside=[](int l) { return Ptr<int(int)>(0x0202ddf4u)(l); };
        const auto island=[](int l) { return Ptr<int(int)>(0x0202ddd4u)(l); };
        if ((!inside(current) || !inside(next)) && (!island(current) || !island(next)) &&
            u8at(0x0209f2d8u)!=2)
            Ptr<void(int)>(0x02013050u)(0x3c);
    } else if (u8at(0x0209f20cu)) {
        call(0x020242c8u); // LC_Update
    } else if (u8at(0x0209f2c4u)) {
        call(0x0202635cu); // PS_Update
    } else {
        const uint32_t pi=u8at(0x020a0e40u);
        const uint16_t h1=at<uint16_t>(0x020a0e58u+pi*4);
        if ((((h1&0x200)==0 && (h1&0x100)==0) || (at<uint16_t>(0x020a0e5au+pi*4)&8)==0) &&
            u8at(0x0209d660u)==0 && at<int32_t>(0x0209d4b0u)==0 &&
            Ptr<int()>(0x0202b818u)()!=0 && Ptr<int()>(0x0202a5c4u)()==0 &&
            uint8_t(u8at(0x0209f294u)|u8at(0x0209f2c4u)|u8at(0x0209f20cu))==0 &&
            at<uint32_t>(0x0209b454u)==0) {
            const uint8_t st=u8at(0x0209f2d8u);
            const bool vs=st==1;
            if (((vs && at<int32_t>(0x0209fc68u)==0) || (at<int32_t>(0x0209caa0u+8)&0x80)) &&
                st!=2 && at<int32_t>(0x0209fc48u)==0 && at<uint16_t>(0x0209f300u)==0) {
                const uint32_t pj=u8at(0x020a0e40u);
                const uint16_t h2=at<uint16_t>(0x020a0e5au+pj*4);
                const int t8=h2&8;
                if ((t8 && (at<uint16_t>(0x020a0e58u+pj*4)&4)==0) || (!vs && !t8 && (h2&4))) {
                    u8at(0x0209f2c4u)=1;
                    sound(2);
                    if (at<uint8_t>(0x0209d4c8u+8)) {
                        Ptr<void(uint32_t)>(0x020195c0u)(0x0209d4c8u);
                        u8at(0x0209f2a0u)=1;
                    }
                    u8at(0x0209f218u)=(at<uint16_t>(0x020a0e58u+u8at(0x020a0e40u)*4)&4) ? 1 : 0;
                    call(0x0202942cu); // PS_Init
                    call(0x0202635cu); // PS_Update
                }
            }
        }
    }
    if (uint8_t(u8at(0x0209f294u)|u8at(0x0209f2c4u)|u8at(0x0209f20cu))==0 &&
        (at<uint32_t>(0x0209b454u)&~0x20000000u)==0)
        call(0x02015e14u);
    return 1;
}
void stage_particle_contents_update(uint32_t self) {
    // Complete Particle::SysTracker::Contents::Update (Particle__SysTracker.h):
    // 0x40 entries of 0x1c at +8; callback_c::OnUpdate is vtable slot 1.
    uint32_t entry=self+8;
    for (int i=0;i<0x40;++i,entry+=0x1c) {
        if (!at<uint32_t>(entry)) continue;
        if (const uint32_t callback=at<uint32_t>(entry+0x10)) {
            const bool active=at<uint8_t>(entry+0xa)==1;
            if (stage_virtual<int>(callback,1,at<uint32_t>(entry+0xc),uint32_t(active)))
                at<uint8_t>(entry+0xa)=1;
        }
        if (at<uint8_t>(entry+0xa)==1) { at<uint8_t>(entry+0xa)=0; continue; }
        Ptr<void(uint32_t,uint32_t)>(0x02021a04u)(self,entry); // Contents::Unlink
        at<uint8_t>(self+4)=uint8_t(i);
    }
}
void stage_particle_tracker_update(uint32_t self) {
    // Complete Particle::SysTracker::Update: mContents (0x8) then the manager.
    Ptr<void(uint32_t)>(0x02021becu)(self+8);
    Ptr<void(uint32_t)>(0x02049f58u)(at<uint32_t>(self+4));
}
// Recovered empty bodies that return 1 / do nothing (e.g. daChScene_c::
// Render and CleanupResources, src/game/actors/d_a_ch_scene.cpp).
int stage_return_one(uint32_t) { return 1; }
void stage_no_op(uint32_t) {}
// Render bodies that only draw their dExtSimpleModel_c member, e.g.
// daObjMc_Metalnet_c::Render: mModel.Render(0) (Model::Render 0x02016b78).
template<uint32_t Model> int stage_render_member_model(uint32_t self) {
    Ptr<void(uint32_t,uint32_t)>(0x02016b78u)(self+Model,0);
    return 1;
}
int stage_set_se_behavior(uint32_t self) {
    // Complete daSetSE_c::Behavior (src/game/actors/d_a_set_se.cpp):
    // param1 (0x08) picks the ambient effect; mSoundHandle 0xd4, mCamSpacePos 0x74.
    if (at<uint8_t>(0x02110aecu)) return 1;
    const uint32_t effect=at<uint32_t>(self+8);
    if (effect>=1 && effect<=4) {
        const int music=at<int32_t>(0x0209b4acu);
        if (music==0x32 || music==0x33 || music==0x34 ||
            Ptr<int(uint32_t)>(0x0200d890u)(at<uint32_t>(0x0209f318u))) // Camera::IsUnderwater
            return 1;
    }
    at<uint32_t>(self+0xd4)=Ptr<uint32_t(uint32_t,uint32_t,uint32_t,uint32_t,int)>(0x02012328u)(
        at<uint32_t>(self+0xd4),3,at<uint32_t>(0x0210b498u+effect*4),self+0x74,0);
    return 1;
}
int stage_texture_sequence_update(uint32_t self,uint32_t model) {
    // Complete TextureSequence::Update(ModelComponents &) (0x02015988):
    // apply the BTP (file at 0x10) at the integer current frame (0x08).
    Ptr<void(uint32_t,uint32_t,int)>(0x02046bbcu)(model,at<uint32_t>(self+0x10),
                                                   uint16_t(uint32_t(at<int32_t>(self+8))>>12));
    return 0;
}
int stage_player_has_no_cap(uint32_t self);
// Translated from the cartridge's ARM code (no recovered source):
// func_ov002_020bea7c -- mCapFlags (0x73c) != 0.
int stage_player_cap_changing(uint32_t self) { return at<uint16_t>(self+0x73c)!=0; }
// func_ov002_020d225c -- character 3 (Yoshi, param1) in mode 2 (0x721)
// with the flag at 0x6e6 clear.
int stage_player_sleep_bubble(uint32_t self) {
    return at<uint32_t>(self+8)==3 && at<uint8_t>(self+0x721)==2 && at<uint8_t>(self+0x6e6)==0;
}
uint32_t stage_player_head_model_id(uint32_t self,uint32_t id,uint32_t drawn) {
    // func_ov002_020becf4 (0xa4 bytes, no recovered source): translated
    // instruction by instruction from the cartridge's ARM code. Selects the
    // head-model slot: 9/8 override flags, then a +1/+4 cap variant.
    uint32_t r0;
    if (drawn==0) {
        if (at<uint8_t>(self+0x6f9)) id=9;
        if (at<uint8_t>(self+0x6fd)) id=8;
        r0=uint32_t(stage_player_has_no_cap(self));
    } else {
        if (at<uint8_t>(self+0x6fa)) id=9;
        if (at<uint8_t>(self+0x6fe)) id=8;
        r0=at<uint8_t>(self+0x701);
    }
    uint32_t r1=0;
    if (id>=4) { if (r0) r1=1; }
    else if (id==3) { if (at<uint8_t>(self+0x714)) r1=4; }
    else if (r0) r1=4;
    return (r1+id)&0xff;
}
int stage_player_behavior(uint32_t self) {
    // Complete Player::Behavior (ov002 0x020e4d24; external/sm64ds-pc/
    // src/_ZN6Player8BehaviorEv.cpp) with Player.h/dActor_c.h/dCc_c.h
    // offsets. The state machine runs through the current State's mMain PMF.
    auto &space=packed::Space::current();
    const auto u8f=[self](uint32_t o) -> uint8_t & { return at<uint8_t>(self+o); };
    const auto call=[self](uint32_t a) { return Ptr<int(uint32_t)>(a)(self); };
    const auto isState=[self](uint32_t st) { return Ptr<int(uint32_t,uint32_t)>(0x020e308cu)(self,st)!=0; };
    const auto changeState=[self](uint32_t st) { Ptr<int(uint32_t,uint32_t)>(0x020e30a0u)(self,st); };
    const auto dec=[self](uint32_t o) { return Ptr<uint16_t(uint32_t)>(0x0203adbcu)(self+o); };
    const auto voice=[self](uint32_t id) {
        Ptr<uint32_t(uint32_t,uint32_t,uint32_t)>(0x02012154u)(at<uint8_t>(self+0x6d9),id,self+0x74);
    };
    if (at<uint8_t>(0x0209f2d8u)==1 && at<uint8_t>(0x0209fc5cu+u8f(0x6d8))==0) return 1;
    int32_t scale=0x1000,r2=0x1000;
    if (u8f(0x703)) { scale=0x3000; r2=0x4200; }
    else if (u8f(0x6fd)) { scale=0x1400; r2=0x2000; }
    const int32_t temp=int32_t((int64_t(r2)*0x32000+0x800)>>12);
    Ptr<void(uint32_t,int32_t)>(0x0203568cu)(self+0x380,temp);
    Ptr<void(uint32_t,int32_t)>(0x02035684u)(self+0x380,temp);
    const uint32_t cc=self+0x2d4;
    at<int32_t>(cc+4)=scale*0x28;
    int32_t mul=0x96;
    for (uint32_t st : {0x021104e4u,0x02110514u,0x02110334u,0x0211031cu,0x021105bcu,0x0211061cu,0x02110634u})
        if (isState(st)) { mul=0x5a; break; }
    at<int32_t>(cc+8)=mul*scale;
    if (u8f(0x703)) at<uint32_t>(cc+0x18)|=0x10; else at<uint32_t>(cc+0x18)&=~0x10u;
    if (at<int32_t>(0x0209fc68u)==0 && at<int32_t>(0x0209fc48u)==0) at<uint8_t>(0x020a0e40u)=0;
    else at<uint8_t>(0x020a0e40u)=u8f(0x6d8);
    if (call(0x020c4188u)) return 1;
    for (uint32_t o : {0x6a4u,0x6a6u,0x6a0u,0x6a8u,0x6aau,0x6acu,0x6b0u,0x6b2u,0x6b4u,0x6b6u,0x6b8u,0x6bau,0x6bcu,0x6c4u,0x6c8u})
        dec(o);
    if (at<uint16_t>(self+0x6c8)==1) at<uint8_t>(0x0209f284u)=0;
    if (u8f(0x6f6)==0) {
        dec(0x6c0);
        if (dec(0x6be)==0) call(0x020d80d0u);
        if (dec(0x6ae)==0) { call(0x020e032cu); call(0x020bdef0u); call(0x020bdd9cu); }
        if (dec(0x6c2)==0) call(0x020bdd2cu);
        if (at<uint16_t>(self+0x6a2)!=uint16_t(int16_t(at<int8_t>(self+0xcc)))) {
            at<uint16_t>(self+0x6a2)=uint16_t(int16_t(at<int8_t>(self+0xcc)));
            at<uint16_t>(self+0x6c6)=0;
        }
        if (dec(0x6c6)==0 && at<int32_t>(self+0x360)!=0) call(0x020d6790u);
    }
    {
        auto &desired=at<int16_t>(self+0x6d2);
        at<int16_t>(self+0x6d4)=desired;
        desired=at<int16_t>(0x0209f4a6u+at<uint8_t>(0x020a0e40u)*0x18);
        const int playerArg=at<int32_t>(0x0209fc68u)==0 ? 0 : u8f(0x6d8);
        desired=int16_t(desired+Ptr<int16_t(int)>(0x0203db24u)(playerArg));
    }
    call(0x020c2e78u); call(0x020ca940u); call(0x020d8158u);
    if (const uint32_t state=at<uint32_t>(self+0x370); state && at<uint32_t>(state+8)) {
        packed::MemberCall member{};
        std::memcpy(&member,space.data(state+8,8),8);
        const auto target=space.resolve_member(self,member);
        using Callback=int (*)(uint32_t);
        Callback callback=nullptr;
        std::memcpy(&callback,&target.code,sizeof callback);
        callback(target.object);
    }
    if (at<uint16_t>(self+0x6ce)&0x80) {
        changeState(0x021104fcu);
        at<uint16_t>(self+0x6ce)&=uint16_t(~0x80u);
    }
    int fallFlag=u8f(0x709)!=0;
    if (at<int8_t>(0x0209f2f8u)==0x1f && isState(0x021104b4u)) fallFlag=0;
    if (u8f(0x6de)==0 || fallFlag || at<int32_t>(self+0x684)<at<int32_t>(self+0x60))
        at<int32_t>(self+0x684)=at<int32_t>(self+0x60);
    if (u8f(0x6de)==0 && u8f(0x70d)) { voice(0x2e); u8f(0x70d)=0; }
    if (u8f(0x6de) && !u8f(0x70d) && !isState(0x02110454u) &&
        at<int32_t>(self+0x684)-at<int32_t>(self+0x60)>0x47e000) {
        u8f(0x70d)=1; voice(0x1d);
    }
    if (u8f(0x707) && !u8f(0x706) && !u8f(0x6de))
        Ptr<void(uint32_t,int32_t)>(0x020ce8bcu)(self,at<int32_t>(self+0x98));
    at<int16_t>(self+0x6d6)=at<int16_t>(self+0x8e);
    at<uint16_t>(self+0x6ce)&=uint16_t(~0x440u);
    if (u8f(0x6ff)) Ptr<void(uint32_t)>(0x02015c3cu)(self+0x174+0x50); // mModelAnim4.Advance()
    for (unsigned lane=0;lane<3;++lane) at<int32_t>(self+0x548+lane*4)=at<int32_t>(self+0x5c+lane*4);
    Ptr<void(uint32_t,uint32_t)>(0x020bf36cu)(self,self+0x2d4);
    call(0x020bf13cu);
    int32_t offset[3]={0,0,0};
    if (isState(0x0210ffecu)) offset[1]=u8f(0x703) ? -0x12c000 : -0x64000;
    else if (isState(0x021106f4u)) offset[1]=0x64000;
    Ptr<void(uint32_t,Ptr<int32_t>)>(0x0201489cu)(self+0x2d4,Ptr<int32_t>(offset));
    call(0x020c2db8u);
    if (u8f(0x707)) at<int32_t>(self+0x66c)=3;
    if (at<int32_t>(0x0209ee90u+0x26c)) {
        at<int32_t>(0x0209ee90u+0x26c)=0;
        changeState(0x021101b4u);
        u8f(0x6e3)=0;
        changeState(0x0210ffecu);
    }
    call(0x020db704u); call(0x020d869cu);
    if (const uint16_t t=at<uint16_t>(self+0x6a0)) {
        if (t==1) at<uint32_t>(cc+0x1c)|=0x10000000u; else at<uint32_t>(cc+0x1c)&=~0x10000000u;
    }
    Ptr<void(uint32_t)>(0x02015024u)(cc);
    if (u8f(0x713)) Ptr<void(uint32_t)>(0x02014ff0u)(cc);
    at<uint8_t>(0x020a0e40u)=at<int32_t>(0x0209fc68u)==0 ? 0 : at<uint8_t>(0x0209f250u);
    call(0x020e4bb8u);
    if (int32_t r=at<int32_t>(self+0x654)) {
        const uint32_t ch=u8f(0x714);
        const uint32_t t4=at<uint32_t>(at<uint32_t>(self+((ch<<2)+3)*4+0x154)+0x14)+0x90;
        if (r<0) r=-r;
        at<int32_t>(t4+0x2c)=r>>3;
    }
    at<uint8_t>(0x0209f254u)=0;
    if (at<uint32_t>(self+8)==3) {
        if (const uint32_t held=at<uint32_t>(at<uint32_t>(self+0x588)))
            at<uint8_t>(0x0209f254u)=at<uint8_t>(held+0x428);
    }
    return 1;
}
int stage_player_render(uint32_t self) {
    // Complete Player::Render (ov002 0x020e3a08; external/sm64ds-pc/src/
    // _ZN6Player6RenderEv.cpp) with Player.h offsets: mBodyModels 0xdc,
    // mModelAnim3 0xf0, mHeadModels 0x154, mModelAnim4 0x174, mSleepBubble
    // 0x1d8, mTexSeqPlayer 0x254 (0x14 each, currFrame +8), mSleepBubbleScale
    // 0x56c, mVsYoshiMaterialParam 0x61c, mInvincibleTimer 0x6a0, mPlayerNo
    // 0x6d8, mBodyModelId 0x6db, mOpacity 0x6f5, mPlayerTexFrame 0x6fc,
    // mIsBalloon 0x6fd, mDrawnHasWings 0x700; mScale 0x80; mFlags 0xb0.
    const auto virt=[](uint32_t object,unsigned slot,uint32_t a,uint32_t b) {
        Ptr<void(uint32_t,uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(object)+slot*4))(object,a,b);
    };
    const auto opacity=[](uint32_t model,uint32_t value) { Ptr<void(uint32_t,uint32_t,int)>(0x02016a9cu)(model,value,0); };
    const auto texseq=[](uint32_t seq,uint32_t components) { Ptr<int(uint32_t,uint32_t)>(0x02015988u)(seq,components); };
    const auto vsMaterials=[self](uint32_t model) {
        if (at<uint8_t>(0x0209f2d8u)!=1 || at<uint32_t>(self+8)!=3) return;
        const uint32_t hdr=at<uint32_t>(model+8);
        uint32_t rec=at<uint32_t>(model+12);
        for (uint32_t i=0;i<at<uint32_t>(hdr+0x24);++i,rec+=0x30) at<int32_t>(rec+0x20)=at<int32_t>(self+0x61c);
    };
    if (at<uint8_t>(0x0209f2d8u)==1 && at<uint8_t>(0x0209fc5cu+at<uint8_t>(self+0x6d8))==0) return 1;
    const uint16_t invincible=at<uint16_t>(self+0x6a0);
    if (Ptr<int(uint32_t,uint32_t)>(0x020e308cu)(self,0x021100c4u)) { if (invincible&2) return 1; }
    else if (invincible&1) return 1;
    const uint8_t alpha=at<uint8_t>(self+0x6f5);
    if (!alpha) return 1;
    if (at<uint32_t>(self+0xb0)&0x10) return 1;
    const uint8_t bodyId=at<uint8_t>(self+0x6db);
    const uint32_t body=at<uint32_t>(self+0xdc+4*Ptr<uint32_t(uint32_t,uint32_t,int)>(0x020becc8u)(self,bodyId,1));
    if (at<uint8_t>(self+0x6fd)==0) {
        opacity(body,alpha);
        vsMaterials(body);
        Ptr<void(uint32_t,uint32_t)>(0x02016b78u)(body,self+0x80);
        if (at<uint32_t>(self+8)==1) {
            texseq(self+0x254,at<uint32_t>(self+0xdc+4)+8);
            at<int32_t>(self+0x254+8)=int32_t(at<uint8_t>(self+0x6fc))<<12;
        }
        Ptr<void(uint32_t,uint32_t,uint32_t)>(0x020e3e00u)(body,self+0x80,alpha);
        if (at<uint8_t>(self+0x700)) {
            opacity(self+0x174,alpha);
            if (bodyId==3) {
                Ptr<void(uint32_t,int,int,int)>(0x0203bf44u)(0x020a0e68u,-0x1b33,-0x666,0);
                Ptr<void(uint32_t,int)>(0x0203bf90u)(0x020a0e68u,0x4000);
                Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02052914u)(0x020a0e68u,at<uint32_t>(body+0x14)+0x180,0x020a0e68u);
                virt(self+0x174,6,0x020a0e68u,self+0x80);
            } else {
                virt(self+0x174,6,at<uint32_t>(body+0x14)+0x2d0,self+0x80);
            }
        }
    } else {
        opacity(self+0xf0,alpha);
        virt(self+0xf0,5,self+0x80,0);
    }
    const uint32_t i=Ptr<uint32_t(uint32_t,uint32_t,int)>(0x020becf4u)(self,bodyId,1);
    const uint32_t head=at<uint32_t>(self+0x154+i*4);
    if (!head || i==9 || i==8) return 1;
    opacity(head,alpha);
    vsMaterials(head);
    if (i==3) {
        Ptr<void(uint32_t,uint32_t)>(0x02016b78u)(head,self+0x80);
    } else {
        std::memcpy(packed::Space::current().data(at<uint32_t>(head+0x14),0x30),
                    packed::Space::current().data(at<uint32_t>(body+0x14)+0x2d0,0x30),0x30);
        virt(head,5,self+0x80,0);
    }
    if (at<uint32_t>(self+8)==1) {
        texseq(self+0x268,at<uint32_t>(self+0x154+4)+8);
        at<int32_t>(self+0x268+8)=int32_t(at<uint8_t>(self+0x6fc))<<12;
    }
    if (Ptr<int(uint32_t)>(0x020bea7cu)(self)==0) texseq(self+0x1dc+bodyId*0x14,head+8);
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x020e3e00u)(head,self+0x80,alpha);
    if (Ptr<int(uint32_t)>(0x020d225cu)(self)) virt(at<uint32_t>(self+0x1d8),5,self+0x56c,0);
    return 1;
}
int stage_player_has_no_cap(uint32_t self) {
    // Complete Player::HasNoCap (src/game/player/Player.cpp): fBase param1
    // at 0x08, mState at PLAYER_STATE, mHasNoCap at 0x71a.
    if (at<int8_t>(0x0209f2f8u)==0x1f) return 0;
    if (at<uint8_t>(0x0209f2d8u)!=2 && at<uint32_t>(self+0x370)!=0x0211058cu &&
        at<uint8_t>(0x0209f2d8u)!=1 && Ptr<int()>(0x02013b18u)())
        return 1;
    if (at<uint32_t>(self+8)==3) return 0;
    return at<uint8_t>(self+0x71a)!=0;
}
int stage_player_is_collecting_cap(uint32_t self) {
    // Complete Player::IsCollectingCap: mCapFlags (0x73c) phases 1..4.
    const uint16_t v=at<uint16_t>(self+0x73c);
    return v!=0 && (v&7)<5;
}
void stage_render_model_transparent(uint32_t self) {
    // Complete Stage::RenderModelTransparent (0x0202b164): the second pass
    // enables only the non-opaque materials of active texture-animation slots.
    const uint32_t components=self+0x874;
    uint32_t arr=at<uint32_t>(at<uint32_t>(components)+8),slot=self+0x8bc;
    const int count=at<uint8_t>(at<uint32_t>(0x0209f340u)+0x14);
    for (int i=0;i<count;++i,slot+=0xc,arr+=0x40) {
        if (!at<uint8_t>(slot+4)) continue;
        const uint32_t ids=at<uint32_t>(arr+0x34);
        for (uint16_t j=0;j<at<uint16_t>(arr+0x30);++j) {
            auto &flags=at<uint32_t>(at<uint32_t>(components+4)+at<uint8_t>(ids+j)*0x30+0x24);
            if ((flags&0x1f0000)!=0x1f0000) flags&=~0x80000000u; else flags|=0x80000000u;
        }
    }
    stage_virtual<void>(self+0x86c,5,0x020755d4u);
}
void stage_shadow_render_all() {
    // Complete static ShadowModel::RenderAll (ShadowModel.h: data 0x08, mat
    // 0x0c, scale 0x10, opacity 0x1c, next 0x24); scratch matrix data_020a0e68.
    constexpr uint32_t scratch=0x020a0e68u;
    for (uint32_t node=at<uint32_t>(0x0209cef4u);node;node=at<uint32_t>(node+0x24)) {
        const uint32_t model=at<uint32_t>(node+0x08);
        Ptr<void(uint32_t,int32_t,int32_t,int32_t)>(0x0203c178u)(scratch,
            at<int32_t>(node+0x10),at<int32_t>(node+0x14),at<int32_t>(node+0x18));
        Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02052914u)(scratch,at<uint32_t>(node+0x0c),scratch);
        at<int32_t>(scratch+0x28)+=0x2000; // t.y
        Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02052914u)(scratch,0x0209b3ecu,scratch);
        const uint32_t opacity=at<uint8_t>(node+0x1c);
        Ptr<void(uint32_t,uint32_t)>(0x02046120u)(model,opacity);
        Ptr<void(uint32_t,uint32_t,uint32_t)>(0x020443c8u)(model,scratch,0);
        Ptr<void(uint32_t,uint32_t,int)>(0x02046088u)(model,opacity,1);
        Ptr<void(uint32_t,uint32_t,uint32_t)>(0x020443c8u)(model,scratch,0);
    }
    at<uint8_t>(0x0209ceecu)=1;
}
void stage_render_model(uint32_t self) {
    // Complete Stage::RenderModel (0x0202b268; recovered isolated unit):
    // mModel 0x86c, its ModelComponents at 0x874 (modelFile, materials of
    // 0x30 bytes), per-slot material groups of 0x40 at *(modelFile+8).
    const uint32_t components=self+0x874;
    uint32_t inner=at<uint32_t>(at<uint32_t>(components)+8);
    const uint32_t materials=at<uint32_t>(components+4);
    uint32_t slot=self+0x8bc;
    const int count=at<uint8_t>(at<uint32_t>(0x0209f340u)+0x14);
    for (int i=0;i<count;++i,slot+=0xc,inner+=0x40) {
        const uint16_t n=at<uint16_t>(inner+0x30);
        const uint32_t ids=at<uint32_t>(inner+0x34);
        for (uint16_t j=0;j<n;++j) {
            auto &flags=at<uint32_t>(materials+at<uint8_t>(ids+j)*0x30+0x24);
            if (at<uint8_t>(slot+4) && (flags&0x1f0000)==0x1f0000) flags&=~0x80000000u;
            else flags|=0x80000000u;
        }
        if (at<uint8_t>(slot+4) && at<uint32_t>(slot))
            Ptr<void(uint32_t,uint32_t)>(0x02015888u)(at<uint32_t>(slot),components);
    }
    stage_virtual<void>(self+0x86c,5,0x020755d4u); // Model::Render(&data_020755d4)
}
void stage_oam_render_sub(uint32_t attr,int32_t x,int32_t y) {
    // Complete static OAM::RenderSub(OamAttr *, int, int) (0x0202194c).
    Ptr<void(int,uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t)>(0x02020994u)(
        1,attr,x,y,-1,-1,0x1000,0x1000,0,-1);
}
int stage_render(uint32_t self) {
    // Complete Stage::Render (0x0202b8a4; external/sm64ds-pc/src/_ZN5Stage6RenderEv.cpp).
    const auto u8at=[](uint32_t a) { return at<uint8_t>(a); };
    if (((u8at(0x0209f294u)|u8at(0x0209f2c4u)|u8at(0x0209f20cu))&0xff)==0) {
        uint32_t slot=self+0x8bc;
        const int count=at<uint8_t>(at<uint32_t>(0x0209f340u)+0x14);
        for (int i=0;i<count;++i,slot+=0xc)
            if (at<uint8_t>(slot+4) && at<uint32_t>(slot))
                Ptr<void(uint32_t)>(0x02015c3cu)(at<uint32_t>(slot)); // Animation::Advance
        Ptr<void(uint32_t)>(0x02022f20u)(self+0x50); // Particle::SysTracker::Update
    }
    Ptr<void()>(0x020391f0u)();
    Ptr<void(uint32_t)>(0x0202b67cu)(self); // RenderFog
    if (const uint32_t sky=at<uint32_t>(self+0x9bc)) {
        const uint32_t src=at<uint32_t>(0x0209f318u)+0x8c;
        for (unsigned lane=0;lane<3;++lane)
            at<int32_t>(sky+0x1c+(9+lane)*4)=at<int32_t>(src+lane*4)>>3;
        stage_virtual<void>(at<uint32_t>(self+0x9bc),5,0); // Draw(0)
    }
    Ptr<void(uint32_t)>(0x0202b268u)(self); // RenderModel
    Ptr<void()>(0x02015d38u)();            // ShadowModel::RenderAll
    Ptr<void(uint32_t)>(0x0202b164u)(self); // RenderModelTransparent
    if (u8at(0x0209f2d8u)==1) {
        if (u8at(0x0209fc9cu)==0 && u8at(0x0209f2c4u)==0) {
            Ptr<void()>(0x0202a168u)();
            Ptr<void()>(0x02029ef8u)();
            if (u8at(0x0209f294u)!=0 && u8at(0x0209f290u)!=2) Ptr<void()>(0x02023be0u)();
        }
        Ptr<void()>(0x0203083cu)();
    }
    Ptr<void()>(0x02014aa8u)();   // dCc_c::Process
    Ptr<void()>(0x020aaf40u)();   // func_ov001_020aaf40
    {
        int x=0,y=0;
        const int n=u8at(0x0209f21cu);
        for (int j=0;j<n;++j) {
            const uint32_t p=at<uint32_t>(0x0209f394u+j*4);
            if (!p) continue;
            const uint8_t bit=at<uint8_t>(p+0x6ff);
            x^=uint8_t(at<int32_t>(p+0x64)^(at<int32_t>(p+0x5c)^at<int32_t>(p+0x60)));
            y|=uint8_t((bit&1)<<j);
        }
        Ptr<void(int,int)>(0x0203db3cu)(0x10,x);
        Ptr<void(int,int)>(0x0203db3cu)(0x10,y);
    }
    if (u8at(0x0209f2c4u)) {
        Ptr<void()>(0x020251d0u)(); // PS_Render
    } else if (u8at(0x0209f20cu) && u8at(0x0209f2d4u)) {
        int v=Ptr<int(int)>(0x02013558u)(at<int8_t>(0x02092124u))<0xf ? 0x80 : 0x70;
        if (u8at(0x0209f2b0u)) v+=0x10;
        using OamRender=int(int,uint32_t,int,int,int,int,uint32_t);
        Ptr<OamRender>(0x020214a4u)(0,0x020abad8u,0x68,v,-1,-1,0);
        Ptr<OamRender>(0x020214a4u)(0,0x020ab9c8u,0x78,v+8,-1,-1,0);
        Ptr<void(int,int,int,int,int)>(0x020250d0u)(u8at(0x0209f260u),0x80,v,1,8);
    }
    Ptr<void()>(0x02023db8u)(); // LC_Render
    return 1;
}
void stage_load_skybox(uint32_t self) {
    // Complete Stage::LoadSkybox (recovered isolated unit); mSkyboxModel 0x9bc.
    const uint32_t skyboxID=Ptr<uint32_t(uint32_t)>(0x0202a93cu)(self);
    if (!skyboxID) return;
    uint32_t model=Ptr<uint32_t(uint32_t)>(0x0203cbe4u)(0x50);
    if (model) model=Ptr<uint32_t(uint32_t)>(0x02016d58u)(model);
    at<uint32_t>(self+0x9bc)=model;
    const uint16_t fileID=at<uint16_t>(0x02075620u+(skyboxID-1)*2);
    Ptr<void(uint32_t,uint32_t,int32_t,int32_t)>(0x02016f9cu)(at<uint32_t>(self+0x9bc),fileID,0,2);
}
uint32_t stage_texture_transformer_ctor(uint32_t self) {
    // Complete TextureTransformer::TextureTransformer() (0x02015950):
    // Animation base (currFrame 0, speed 0x1000), _ZTV18TextureTransformer,
    // file = 0 (TextureTransformer.h: 0x14 bytes).
    (void)packed::Space::current().data(self,0x14);
    stage_frame_construct(self,0x0208e7c4u);
    at<uint32_t>(self+0x10)=0;
    return self;
}
void stage_load_texture_transformers(uint32_t self) {
    // Complete Stage::LoadTextureTransformers (recovered isolated unit):
    // mModel.data.modelFile at 0x874, mTexAnimSlots[8] (0xc each) at 0x8bc.
    const uint32_t info=at<uint32_t>(0x0209f340u);
    uint32_t entry=at<uint32_t>(info+0x10),slot=self+0x8bc;
    const uint32_t bmd=at<uint32_t>(self+0x874);
    const int count=at<uint8_t>(info+0x14);
    for (int i=0;i<count;++i,entry+=12,slot+=0xc) {
        const uint32_t bta=at<uint32_t>(entry+4);
        if (!bta) continue;
        Ptr<void(uint32_t,uint32_t)>(0x0201587cu)(bmd,bta);
        uint32_t t=Ptr<uint32_t(uint32_t)>(0x0203cbe4u)(0x14);
        if (t) t=Ptr<uint32_t(uint32_t)>(0x02015950u)(t);
        at<uint32_t>(slot)=t;
        Ptr<void(uint32_t,uint32_t,int32_t,int32_t,uint32_t)>(0x020158acu)(at<uint32_t>(slot),bta,0,0x1000,0);
    }
}
uint32_t stage_clipper_ctor(uint32_t self) {
    // Complete Clipper::Clipper() (external/sm64ds-pc/src/_ZN7ClipperC1Ev.cpp):
    // the ROM stores _ZTV7Clipper, then the shared frustum init helper.
    at<uint32_t>(self)=0x0208e730u;
    Ptr<void(uint32_t,int32_t,uint32_t,int32_t,int32_t)>(0x020156dcu)(self,0x1555,0xe38,0x1000,0x1388000);
    return self;
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
// Handles the game binds dynamically (NNSSndHandle words in its effect
// slot tables, e.g. data_0209b53c) start through NNS_SndArcPlayerStartSeqArc.
// As in the SDK the handle word points at the bound NNSSndSeqPlayer: a
// cartridge-visible shadow record per native effect player, holding the
// fields recovered game code reads through the handle (seqNo +0x3a,
// SND player number +0x3c, volume +0x41; see func_020485f0). Each record
// owns one native effect player; a finished voice clears its handle.
constexpr uint32_t StageSeqPlayerBytes=0x50;
struct StageVoice { uint32_t record=0, handle=0, token=0; bool active=false, stopping=false; };
std::array<StageVoice,16> &stage_voices() { static std::array<StageVoice,16> voices; return voices; }
uint32_t stageNextVoice=0x400000;
void stage_retire_voice(StageVoice &voice) {
    if (voice.handle && at<uint32_t>(voice.handle)==voice.record) at<uint32_t>(voice.handle)=0;
    voice.handle=0; voice.active=false; voice.stopping=false;
}
void stage_retire_finished_voices() {
    auto &voices=stage_voices();
    for (int player=0;player<16;++player)
        if (voices[player].active &&
            !GameProgram::active().native_audio({SoundOp::Status,player,voices[player].token}))
            stage_retire_voice(voices[player]);
}
int stage_voice_for_handle(uint32_t handle) {
    const uint32_t record=at<uint32_t>(handle);
    if (!record) return -1;
    auto &voices=stage_voices();
    for (int player=0;player<16;++player)
        if (voices[player].active && voices[player].record==record && voices[player].handle==handle)
            return player;
    return -1;
}
int stage_voice_for_record(uint32_t record) {
    auto &voices=stage_voices();
    for (int player=0;player<16;++player)
        if (voices[player].active && voices[player].record==record) return player;
    return -1;
}
// func_02050cdc: &NNSSndArcSeqArcSeqInfo::param for (archive, index), or 0.
// The SDK reads its INFO block; here the same 8 SSAR record bytes come from
// the native SDAT owner into one stable cartridge record per entry.
std::map<uint32_t,uint32_t> &stage_seqarc_params() { static std::map<uint32_t,uint32_t> params; return params; }
uint32_t stage_seqarc_param(int archive,int entry) {
    if (archive<0 || entry<0) return 0;
    const uint32_t key=(uint32_t(archive)<<16)|uint32_t(entry&0xffff);
    auto &params=stage_seqarc_params();
    if (auto found=params.find(key); found!=params.end()) return found->second;
    sd_u8 bytes[8];
    if (!pc_seqarc_param(archive,entry,bytes)) return 0;
    const uint32_t record=packed::Space::current().allocate(8,4);
    std::memcpy(packed::Space::current().data(record,8),bytes,8);
    params.emplace(key,record);
    return record;
}
int stage_sound_player(uint32_t handle) {
    if (handle==0x0209b4a0u) return 0;
    if (handle==0x0209b4b0u) return 4;
    const int voice=stage_voice_for_handle(handle);
    if (voice>=0) return voice;
    throw std::runtime_error("Unmapped original NNS sound handle: "+std::to_string(handle));
}
int stage_snd_start_seqarc(uint32_t handle,int archive,int entry) {
    // NNS_SndArcPlayerStartSeqArc (0x02051f1c): the SDK boundary of the
    // original Player_PlaySoundEffect. Players 0/4 are the music layers
    // and 1 the shared 2D effect player; dynamic voices use the others.
    stage_retire_finished_voices();
    auto &voices=stage_voices();
    const int previous=stage_voice_for_handle(handle);
    if (previous>=0) voices[previous].handle=0; // the SDK releases the old binding
    at<uint32_t>(handle)=0;
    int player=-1;
    for (int candidate=2;candidate<16 && player<0;++candidate)
        if (candidate!=4 && !voices[candidate].active) player=candidate;
    if (player<0) {
        // All native effect players busy: the oldest voice yields.
        uint32_t oldest=~0u;
        for (int candidate=2;candidate<16;++candidate)
            if (candidate!=4 && voices[candidate].token<oldest) { oldest=voices[candidate].token; player=candidate; }
        GameProgram::active().native_audio({SoundOp::Stop,player,voices[player].token});
        stage_retire_voice(voices[player]);
    }
    auto &voice=voices[player];
    if (!voice.record) voice.record=packed::Space::current().allocate(StageSeqPlayerBytes,4);
    std::memset(packed::Space::current().data(voice.record,StageSeqPlayerBytes),0,StageSeqPlayerBytes);
    at<uint32_t>(voice.record+0x10)=handle;
    at<uint16_t>(voice.record+0x3a)=uint16_t(entry);
    at<uint8_t>(voice.record+0x3c)=uint8_t(player);
    at<uint8_t>(voice.record+0x41)=0x7f;
    voice.token=stageNextVoice++;
    if (!GameProgram::active().native_audio({SoundOp::Effect,player,voice.token,archive,entry})) return 0;
    voice.handle=handle; voice.active=true; voice.stopping=false;
    at<uint32_t>(handle)=voice.record;
    return 1;
}
int stage_snd_start_seqarc_ex(uint32_t handle,int playerNo,int bankNo,int playerPrio,int archive,int entry) {
    // NNS_SndArcPlayerStartSeqArcEx (0x02051e60). The recovered callers
    // (Player_PlaySoundEffect kind 3) keep the archive's player and bank
    // (-1); the priority only orders SDK player eviction.
    (void)playerPrio;
    if (playerNo>=0 || bankNo>=0)
        throw std::runtime_error("NNS_SndArcPlayerStartSeqArcEx player/bank override is not ported.");
    return stage_snd_start_seqarc(handle,archive,entry);
}
void stage_snd_player_set_volume(uint32_t handle,int volume) {
    // NNS_SndPlayerSetVolume (0x0204f924, src/func_0204f924.c): the byte at
    // seqPlayer+0x41, which the SDK applies as the player volume.
    const uint32_t record=at<uint32_t>(handle);
    if (!record) return;
    at<uint8_t>(record+0x41)=uint8_t(volume);
    const int player=stage_voice_for_record(record);
    if (player>=0) GameProgram::active().native_audio({SoundOp::PlayerParam,player,stage_voices()[player].token,4,uint8_t(volume)});
}
void stage_snd_player_set_track_pan(uint32_t handle,int tracks,int pan) {
    // NNS_SndPlayerSetTrackPan (0x0204f7cc, src/func_0204f7cc.c):
    // SND_SetTrackPan(seqPlayer->playerNo, tracks, pan).
    const uint32_t record=at<uint32_t>(handle);
    if (!record) return;
    const int player=stage_voice_for_record(record);
    if (player>=0) GameProgram::active().native_audio({SoundOp::TrackParam,player,stage_voices()[player].token,tracks,9,pan});
}
int stage_snd_start_seq(uint32_t handle,int sequence) {
    // NNS_SndArcPlayerStartSeq (0x02051fb4).
    return GameProgram::active().native_audio({SoundOp::Music,stage_sound_player(handle),0,sequence})?1:0;
}
int stage_snd_stop_seq(uint32_t handle,int frames) {
    // NNS_SndPlayerStopSeq (0x0204fa2c).
    if (handle!=0x0209b4a0u && handle!=0x0209b4b0u) {
        const int player=stage_voice_for_handle(handle);
        if (player<0) return 1; // unbound handle: nothing to stop
        auto &voice=stage_voices()[player];
        if (!voice.stopping)
            GameProgram::active().native_audio({SoundOp::Stop,player,voice.token,frames});
        voice.stopping=true;
        return 1;
    }
    GameProgram::active().native_audio({SoundOp::Stop,stage_sound_player(handle),0,frames});
    return 1;
}
void stage_sound_play_2d(uint32_t archive,uint32_t entry) {
    // Sound::Play2D (0x02012840, src/engine/sound/Sound.cpp):
    // Player_PlaySoundEffect on the shared effect player (data_0209b4a4),
    // served by the native SDAT sequencer's effect player 1.
    static uint32_t token=0x300000;
    GameProgram::active().native_audio({SoundOp::Effect,1,++token,int(archive),int(entry)});
}
void stage_sound_play_3d(uint32_t archive,uint32_t entry,uint32_t) {
    // Sound::Play(u32, u32, const Vector3 &) (0x02012590). KNOWN GAP: the
    // original SND3D emitter path (func_02048720/func_02048908/
    // func_02048a1c) is not ported yet, so the effect plays on the native
    // effect player without positional attenuation or pan.
    stage_sound_play_2d(archive,entry);
}
void stage_sound_play_bank0(uint32_t entry,uint32_t position) {
    // Complete Sound::PlayBank0 (0x0201264c): Sound::Play(0, id, pos).
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02012590u)(0,entry,position);
}
void stage_sound_play_bank3(uint32_t entry,uint32_t position) {
    // Complete Sound::PlayBank3 (0x02012664): Sound::Play(3, id, pos).
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02012590u)(3,entry,position);
}
uint32_t stage_sound_play_2d_archive2(uint32_t entry) {
    // Complete func_02012790 (external/sm64ds-pc/src/func_02012790.c):
    // Sound::Play2D(2, entry). Play2D's own result is not used by callers.
    stage_sound_play_2d(2,entry);
    return 1;
}
uint32_t stage_sound_play_char_voice(uint32_t character,uint32_t voice,uint32_t position) {
    // Complete Sound::PlayCharVoice: Sound::Play(1, voice + data_02075250[character], v).
    stage_sound_play_3d(1,voice+at<uint8_t>(0x02075250u+character),position);
    return 0;
}
uint32_t stage_sound_play_long(uint32_t handle,uint32_t a,uint32_t b,uint32_t pos,int e) {
    // Complete Sound::PlayLong (src/engine/sound/Sound.cpp): keep or start a
    // looping effect in the slot table at data_0209b53c.
    if (Ptr<int(uint32_t,uint32_t,uint32_t)>(0x02048a1cu)(pos,a,b)==0) return 0;
    if (const uint32_t slot=Ptr<uint32_t(uint32_t,uint32_t)>(0x02011934u)(0x0209b53cu,handle)) {
        at<uint8_t>(slot+6)=1;
        Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x020123c8u)(slot+8,a,b,pos);
        return at<uint32_t>(slot);
    }
    return Ptr<uint32_t(uint32_t,uint32_t,uint32_t,uint32_t,int)>(0x0201179cu)(0x0209b53cu,a,b,pos,e);
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
int stage_approach_linear_int(uint32_t ref,int target,int step) {
    // Complete ApproachLinear(int &, int, int) (src/ApproachAngle.cpp),
    // with the ARM's wrapping 32-bit arithmetic.
    auto &value=at<int32_t>(ref);
    if (step!=0) {
        if (value>target) step=int(0u-uint32_t(step));
        value=int32_t(uint32_t(value)+uint32_t(step));
        if (int64_t(step)*int32_t(uint32_t(value)-uint32_t(target))>=0) { value=target; return 1; }
    } else if (value==target) {
        return 1;
    }
    return 0;
}
int stage_approach_linear_short(uint32_t ref,int16_t target,int16_t step) {
    // Complete ApproachLinear(short &, short, short).
    auto &value=at<int16_t>(ref);
    if (step!=0) {
        int16_t d=int16_t(value-target);
        if (d>0) step=int16_t(-step);
        value=int16_t(value+step);
        d=int16_t(value-target);
        if (step*d>=0) { value=target; return 1; }
    } else if (value==target) {
        return 1;
    }
    return 0;
}
// Recovered destructor bodies (external/sm64ds-pc/src/*D1Ev.cpp). Their
// implicit vptr stores have no observable effect once the object is freed.
void stage_shadow_destruct(uint32_t shadow) {
    // ShadowModel::~ShadowModel: unlink from the list at data_0209cef4.
    const uint32_t prev=at<uint32_t>(shadow+0x20),next=at<uint32_t>(shadow+0x24);
    if (prev) at<uint32_t>(prev+0x24)=next;
    else if (at<uint32_t>(0x0209cef4u)==shadow) at<uint32_t>(0x0209cef4u)=next;
    if (next) at<uint32_t>(next+0x20)=prev;
    at<uint32_t>(shadow+0x20)=at<uint32_t>(shadow+0x24)=0;
}
void stage_model_destruct(uint32_t model) {
    // Model::~Model frees transformsBuf (0x4c); ModelBase::~ModelBase frees
    // its owned modelFile (0x04). Both are native heap allocations here.
    if (const uint32_t transforms=at<uint32_t>(model+0x4c)) packed::Space::current().release(transforms);
    if (const uint32_t file=at<uint32_t>(model+0x04)) packed::Space::current().release(file);
}
uint32_t stage_list_node_d1(uint32_t node) {
    // fLiNdBa_c / fLiNdBaPr_c D1 (0x02044104 / 0x020440e8): a node being
    // destroyed stops being the process walk's cursor (data_020a4b68).
    if (at<uint32_t>(0x020a4b68u)==node) at<uint32_t>(0x020a4b68u)=0;
    return node;
}
void stage_actor_destruct(uint32_t actor) {
    // dActor_c D2 (0x020112c8, cartridge frame): vptr 0x0208e3a4, leave the
    // actor list (data_0209b468) via mActorListNode (+0x50), fLiNdBa_c D1 of
    // that node, then the dBase_c vptr 0x0208e4b8 and fBase_c D2.
    at<uint32_t>(actor)=0x0208e3a4u;
    Ptr<void(uint32_t,uint32_t)>(0x0203b27cu)(0x0209b468u,actor+0x50);
    Ptr<uint32_t(uint32_t)>(0x02044104u)(actor+0x50);
    at<uint32_t>(actor)=0x0208e4b8u;
    Ptr<uint32_t(uint32_t)>(0x02043d48u)(actor);
}
uint32_t stage_mip_destruct(uint32_t actor) {
    // daMip_c::~daMip_c (ov085 0x0212a6d4): members in reverse order --
    // two ShadowModels, ModelAnim, dBgCh_Actr (empty), dCcAc_c
    // (dCc_c::Unlink) -- then dEnemyBase_c/dActor_c.
    stage_shadow_destruct(actor+0x3c0);
    stage_shadow_destruct(actor+0x368);
    stage_model_destruct(actor+0x300);
    Ptr<void(uint32_t)>(0x02014fa4u)(actor+0x110);
    stage_actor_destruct(actor);
    return actor;
}
int stage_clipper_transform_and_clip(uint32_t self,uint32_t mat,uint32_t src,int32_t scale,uint32_t dst) {
    // Complete Clipper::TransformAndClip (0x02015560): transform into camera
    // space, then the frustum test without a plane hint.
    Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02052858u)(src,mat,dst);
    return Ptr<int(uint32_t,uint32_t,int32_t,uint32_t)>(0x020150e8u)(self,dst,scale,0);
}
int stage_tree_render(uint32_t self) {
    // Complete daTree_c::Render (src/game/actors/d_a_tree.cpp): each variant
    // model (0xd4 + i*0x50, mat4x3 at +0x1c) faces the camera yaw
    // (dCamera_c 0x17c) and draws every shared TreeNode that the Clipper
    // keeps within (0x11000, 0x578000), fading in below 0x2f000.
    const uint32_t camera=at<uint32_t>(0x0209f318u);
    for (unsigned i=0;i<5;++i) {
        const uint32_t model=self+0xd4+i*0x50,mat=model+0x1c;
        Ptr<void(uint32_t,int)>(0x0203bd6cu)(mat,at<int16_t>(camera+0x17c));
        for (uint32_t node=at<uint32_t>(0x02110a48u+i*4);node;node=at<uint32_t>(node+0x48)) {
            int32_t out[3];
            const int dist=Ptr<int(uint32_t,uint32_t,uint32_t,int32_t,Ptr<int32_t>)>(0x02015560u)(
                0x0209f43cu,0x0209b3ecu,node,0x1e000,Ptr<int32_t>(out));
            if (dist<=0x11000 || dist>=0x578000) continue;
            const int opacity=dist<0x2f000 ? ((dist-0x10000)>>12)&0xff : 0x1f;
            Ptr<void(uint32_t,uint32_t,uint32_t)>(0x02016a9cu)(model,opacity,0);
            at<int32_t>(mat+0x24)=at<int32_t>(node);
            at<int32_t>(mat+0x28)=at<int32_t>(node+4)-0x1e000;
            at<int32_t>(mat+0x2c)=at<int32_t>(node+8);
            Ptr<void(uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(model)+5*4))(model,0); // Render(0)
        }
    }
    return 1;
}
int stage_tree_cleanup(uint32_t self) {
    // Complete daTree_c::CleanupResources (src/game/actors/d_a_tree.cpp):
    // for each loaded variant (transformsBuf set) free its shared TreeNode
    // list at data_ov002_02110a48; each node owns a dCcPos_c at +0x0c.
    for (unsigned i=0;i<5;++i) {
        if (!at<uint32_t>(self+0xd4+i*0x50+0x4c)) continue;
        const uint32_t slot=0x02110a48u+i*4;
        while (const uint32_t node=at<uint32_t>(slot)) {
            at<uint32_t>(slot)=at<uint32_t>(node+0x48);
            Ptr<void(uint32_t)>(0x02014fa4u)(node+0x0c); // ~dCcPos_c -> dCc_c::Unlink
            packed::Space::current().release(node);      // Memory::operator_delete2
        }
    }
    return 1;
}
uint32_t stage_tree_destruct(uint32_t self) {
    // daTree_c::~daTree_c (0x020ebf8c): five dExtSimpleModel_c members in
    // reverse order, then dActor_c.
    for (int i=4;i>=0;--i) stage_model_destruct(self+0xd4+i*0x50);
    stage_actor_destruct(self);
    return self;
}
int stage_approach_linear2_short(uint32_t ref,int16_t target,int16_t step) {
    // Complete ApproachLinear2(short &, short, short) (src/ApproachAngle.cpp).
    auto &x=at<int16_t>(ref);
    if (step!=0) {
        if (x>target) step=int16_t(-step);
        x=int16_t(x+step);
        if (step*(x-target)>=0) { x=target; return 1; }
    } else if (x==target) {
        return 1;
    }
    return 0;
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
int stage_actor_before_init(uint32_t actor) {
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

#include "stage_structor_frames.inc"

namespace pmf_state_calls {
// The recovered state-table helpers (external/sm64ds-pc/src: `struct Entry
// { PMF pmf[2]; }; (c->*TABLE[c->idx].pmf[K])()`, optionally storing the new
// index first): the PMF of entry idx, slot K, called on the object through
// the checked member-call resolver. Rows: stage_pmf_state_calls.inc.
template<uint32_t Table,uint32_t IndexOffset,unsigned Slot>
void call(uint32_t self) {
    auto &space=packed::Space::current();
    packed::MemberCall member{};
    std::memcpy(&member,space.data(Table+at<uint32_t>(self+IndexOffset)*16+Slot*8,8),8);
    const auto target=space.resolve_member(self,member);
    using Callback=void (*)(uint32_t);
    Callback callback=nullptr;
    std::memcpy(&callback,&target.code,sizeof callback);
    callback(target.object);
}
template<uint32_t Table,uint32_t IndexOffset,unsigned Slot>
void set_and_call(uint32_t self,int index) {
    at<int32_t>(self+IndexOffset)=index;
    call<Table,IndexOffset,Slot>(self);
}
inline void bind(packed::Space &space,int module) {
#define PMF_STATE_CALL(mod,address,table,offset,slot) \
    if (module==(mod)) space.bind(address,&call<table,offset,slot>);
#define PMF_STATE_SET(mod,address,table,offset,slot) \
    if (module==(mod)) space.bind(address,&set_and_call<table,offset,slot>);
#include "stage_pmf_state_calls.inc"
#undef PMF_STATE_CALL
#undef PMF_STATE_SET
}
} // namespace pmf_state_calls

uint32_t stage_cutscene_object_d1(uint32_t self);
uint32_t stage_demo_anm_model_d0(uint32_t self);
uint32_t stage_demo_simple_model_d1(uint32_t self);
uint32_t stage_demo_simple_model_d0(uint32_t self);
void bind_stage_overlay_runtime(packed::Space &space,unsigned id) {
    structor_frames::bind_structor_frames(space,int(id));
    pmf_state_calls::bind(space,int(id));
    if (id==102) {
        space.bind(0x02149610u,&stage_ground_height_below);
    }
    if (id==2) {
        bind_stage_player_runtime(space);
        space.bind(0x020f80acu,&stage_cutscene_object_render);
        space.bind(0x020f1f70u,&stage_cutscene_object_d1);
        space.bind(0x020f6778u,&stage_demo_anm_model_d0);
        space.bind(0x020f69a8u,&stage_demo_simple_model_d0);
        space.bind(0x020f6a00u,&stage_demo_simple_model_d1);
        space.bind(0x020c607cu,&stage_player_vertical_line_probe);
        space.bind(0x020ee674u,&stage_bg_actor_update_kill_by_mega_char);
        space.bind(0x020ef070u,&stage_actor_forward_line_hit);
        space.bind(0x020b0a7cu,&stage_return_one); // daChScene_c::Render
        space.bind(0x020b0a70u,&stage_return_one); // daChScene_c::CleanupResources
        space.bind(0x020b0a78u,&stage_no_op);      // daChScene_c::OnPendingDestroy
        space.bind(0x020f19fcu,&stage_set_se_behavior);
        space.bind(0x020ec020u,&stage_tree_cleanup);
        space.bind(0x020ec0a4u,&stage_tree_render);
        space.bind(0x020ebf8cu,&stage_tree_destruct);
        space.bind(0x020bda98u,&stage_player_has_no_cap);
        space.bind(0x020e3a08u,&stage_player_render);
        space.bind(0x020e4d24u,&stage_player_behavior);
        space.bind(0x020becf4u,&stage_player_head_model_id);
        space.bind(0x020bea7cu,&stage_player_cap_changing);
        space.bind(0x020d225cu,&stage_player_sleep_bubble);
        space.bind(0x020bea94u,&stage_player_is_collecting_cap);
    }
    if(id==102) {
        for(uint32_t factory : {0x0214a7ecu,0x0214a82cu,0x0214a86cu,
                                0x0214a8acu,0x0214a8ecu,0x0214a92cu})
            space.bind(factory,&stage_hatena_factory);
        space.bind(0x02149da8u,&stage_hatena_set_state);
    }
    if (id==9) {
        space.bind(0x02111d8cu,&stage_castle_water_factory);
        space.bind(0x02112048u,&stage_metalnet_factory);
        space.bind(0x02111ea8u,&stage_render_member_model<0xd4>); // daObjMc_Metalnet_c::Render
        space.bind(0x021121f0u,&stage_castle_flag_factory);
        space.bind(0x02111a30u,&stage_bird_factory);
    }
    if (id==85) {
        space.bind(0x0212f244u,&stage_sign_factory);
        space.bind(0x0212cc2cu,&stage_mip_factory);
        space.bind(0x0212a6d4u,&stage_mip_destruct);
        space.bind(0x0212bc78u,&stage_mip_set_state);
        space.bind(0x0212ed54u,&stage_jugem_factory);
        space.bind(0x0212e728u,&stage_jugem_set_state);
        space.bind(0x0212ead0u,&stage_jugem_render);
    }
    if (id==98) space.bind(0x0213b43cu,&stage_cannon_factory);
    if (id==100) {
        space.bind(0x0214589cu,&stage_door_factory);
        space.bind(0x02145550u,&stage_door_behavior);
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
    space.bind(0x020fe154u,&stage_meter_factory);
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

void bind_stage_arm9_math(packed::Space &space) {
    // Stage-only: the minigame owner binds its own cstd::sqrt veneer.
    space.bind(0x0203d744u,&stage_cstd_sqrt);
    space.bind(0x0203b4dcu,&stage_cstd_atan2);
    space.bind(0x01ffabe4u,&stage_aeabi_idiv);
    space.bind(0x01ff98f4u,&stage_float_less);
    space.bind(0x01ff99a4u,&stage_float_greater);
    space.bind(0x01ffa344u,&stage_float_to_int);
    space.bind(0x01ffa4bcu,&stage_int_to_float);
}
void stage_model_virtual10(uint32_t self,uint32_t matrix) {
    // Complete Model::Virtual10(Matrix4x3 &) (0x02016bb8,
    // external/sm64ds-pc/src/_ZN5Model9Virtual10ER9Matrix4x3.cpp): copy the
    // 12-word root matrix into data.transforms[0], then func_02045074(&data,
    // data.modelFile->bones). ModelComponents is at Model+8.
    const uint32_t data=self+8;
    std::memcpy(packed::Space::current().data(at<uint32_t>(data+0xc),0x30),
                packed::Space::current().data(matrix,0x30),0x30);
    Ptr<void(uint32_t,uint32_t)>(0x02045074u)(data,at<uint32_t>(at<uint32_t>(data)+8));
}
void stage_model_anim_virtual10(uint32_t self,uint32_t matrix) {
    // Complete ModelAnim::Virtual10(Matrix4x3 &) (0x0201682c,
    // external/sm64ds-pc/src/_ZN9ModelAnim9Virtual10ER9Matrix4x3.cpp):
    // data.UpdateBones(file, (u32)(currFrame << 4) >> 16), then the Model
    // base. Animation sits at +0x50 (currFrame +0x58), the BCA file at +0x60.
    const int32_t frame=at<int32_t>(self+0x58);
    Ptr<void(uint32_t,uint32_t,int)>(0x02045394u)(self+8,at<uint32_t>(self+0x60),int((uint32_t(frame)<<4)>>16));
    stage_model_virtual10(self,matrix);
}
int stage_cstd_abs(int x) {
    // Complete cstd::abs(int) (0x0206df84, src/_ZN4cstd3absEi.cpp), with the
    // ARM's wrapping negation of INT_MIN.
    return x<0 ? int(0u-uint32_t(x)) : x;
}
uint32_t stage_cc_ac_pos_get_pos(uint32_t self) {
    // Complete dCcAcPos_c::GetPos() (0x02014a2c,
    // external/sm64ds-pc/src/_ZN10dCcAcPos_c6GetPosEv.cpp): &pos (+0x34).
    return self+0x34;
}
// Structors the recovered sources only declare (mwcc emits them from the
// class definitions); each body below is read from the cartridge code.
uint32_t stage_frame_ctrl_d2(uint32_t self) {
    // dExtFrameCtrl_c D2 (0x02015cb4): restore _ZTV15dExtFrameCtrl_c.
    at<uint32_t>(self)=0x0208e7e4u;
    return self;
}
uint32_t stage_texture_sequence_d1(uint32_t self) {
    // TextureSequence D1 (0x02015a2c): its vptr, then the frame-control D2.
    at<uint32_t>(self)=0x0208e7d4u;
    stage_frame_ctrl_d2(self);
    return self;
}
uint32_t stage_texture_sequence_d0(uint32_t self) {
    // TextureSequence D0 (0x02015a00): D1, then Memory::operator_delete2.
    stage_texture_sequence_d1(self);
    Ptr<void(uint32_t)>(0x0203cbccu)(self);
    return self;
}
uint32_t stage_ext_anm_model_d2(uint32_t self) {
    // dExtAnmModel_c D2 (0x0201689c): both vptrs (0x0208e980, Animation base
    // 0x0208e9a4 at +0x50), the frame-control D2 on +0x50, then the model
    // base D2 (0x02016ca8).
    at<uint32_t>(self)=0x0208e980u;
    at<uint32_t>(self+0x50)=0x0208e9a4u;
    stage_frame_ctrl_d2(self+0x50);
    Ptr<void(uint32_t)>(0x02016ca8u)(self);
    return self;
}
uint32_t stage_ext_model_d2(uint32_t self) {
    // dExtModel_c D2 (0x020170b8): vptr 0x0208e87c, then the model-file
    // release (0x02018144) when +4 holds one.
    at<uint32_t>(self)=0x0208e87cu;
    if (at<uint32_t>(self+4)) Ptr<void(uint32_t)>(0x02018144u)(at<uint32_t>(self+4));
    return self;
}
uint32_t stage_model_d2(uint32_t self) {
    // Model D2 (0x02016ca8): vptr 0x0208e90c, free transformsBuf (+0x4c)
    // through func_0203cbc0, then dExtModel_c D2.
    at<uint32_t>(self)=0x0208e90cu;
    if (at<uint32_t>(self+0x4c)) Ptr<void(uint32_t)>(0x0203cbc0u)(at<uint32_t>(self+0x4c));
    return stage_ext_model_d2(self);
}
template<uint32_t (*Body)(uint32_t)>
uint32_t stage_deleting_d0(uint32_t self) {
    // A deleting destructor (D0) as the cartridge emits it: the complete
    // destructor body, then Memory::operator_delete2 (0x0203cbcc).
    Body(self);
    Ptr<void(uint32_t)>(0x0203cbccu)(self);
    return self;
}
void stage_cxa_vec_cleanup(uint32_t base,uint32_t count,uint32_t size,uint32_t destructor) {
    // __cxa_vec_cleanup (0x0207328c): destroy count elements in reverse
    // order. The ROM's handler frame only matters for exceptions, which
    // recovered game code never throws.
    if (!destructor) return;
    for (uint32_t element=base+count*size;count;--count) {
        element-=size;
        (Ptr<void(uint32_t)>(destructor))(element);
    }
}
// Particle::SysTracker::Contents registry (include/Particle__SysTracker.h:
// unk_000 +0, mCurrentIndex +4, 0x40 x 0x1c-byte Entry at +8, mBuckets[16]
// at +0x708; Entry uniqueID +0, definitionID +4, savedValue +8, active +0xa,
// system +0xc, callback +0x10, prev +0x14, next +0x18). Each body follows
// its recovered unit in external/sm64ds-pc/src.
constexpr uint32_t kParticleBuckets=0x708;
uint32_t particle_definition_data(uint32_t definition) {
    // data_0209ee74->mManager (+4)->mDefinitions (+0x1c)[id] (0x20 bytes) .data
    const uint32_t manager=at<uint32_t>(at<uint32_t>(0x0209ee74u)+4);
    return at<uint32_t>(at<uint32_t>(manager+0x1c)+definition*0x20);
}
uint32_t stage_particle_find_data(uint32_t self,uint32_t uniqueID) {
    // Contents::FindData (0x02021b58).
    if (!uniqueID) return 0;
    for (uint32_t entry=at<uint32_t>(self+kParticleBuckets+(uniqueID&0xf)*4);entry;entry=at<uint32_t>(entry+0x18))
        if (at<uint32_t>(entry)==uniqueID) return entry;
    return 0;
}
void stage_particle_entry_reset(uint32_t entry) {
    // Contents::Entry::Reset (0x02021cd4): give the definition back its
    // loaned callback value, flag the System (+0x1c bit 0), free the slot.
    at<uint16_t>(particle_definition_data(at<uint32_t>(entry+4))+0x28)=at<uint16_t>(entry+8);
    at<uint32_t>(at<uint32_t>(entry+0xc)+0x1c)|=1;
    at<uint32_t>(entry)=0;
}
void stage_particle_link(uint32_t self,uint32_t entry) {
    // Contents::Link (0x02021a48).
    const uint32_t bucket=self+kParticleBuckets+(at<uint32_t>(entry)&0xf)*4;
    const uint32_t head=at<uint32_t>(bucket);
    if (!head) {
        at<uint32_t>(bucket)=entry;
    } else {
        at<uint32_t>(head+0x14)=entry;
        at<uint32_t>(entry+0x18)=at<uint32_t>(bucket);
        at<uint32_t>(bucket)=entry;
    }
}
void stage_particle_unlink(uint32_t self,uint32_t entry) {
    // Contents::Unlink (0x02021a04).
    const uint32_t prev=at<uint32_t>(entry+0x14),next=at<uint32_t>(entry+0x18);
    if (!prev) at<uint32_t>(self+kParticleBuckets+(at<uint32_t>(entry)&0xf)*4)=next;
    else at<uint32_t>(prev+0x18)=next;
    if (next) at<uint32_t>(next+0x14)=prev;
    stage_particle_entry_reset(entry);
}
void stage_particle_contents_clear(uint32_t self) {
    // Complete Particle::SysTracker::Contents::Clear (0x02021b98,
    // external/sm64ds-pc/src/_ZN8Particle10SysTracker8Contents5ClearEv.cpp):
    // deactivate and unlink every live entry (0x40 x 0x1c at +8), recording
    // the last index in mCurrentIndex (+4).
    for (uint32_t i=0;i<0x40;++i) {
        const uint32_t entry=self+8+i*0x1c;
        if (!at<uint32_t>(entry)) continue;
        at<uint8_t>(entry+0xa)=0;
        Ptr<void(uint32_t,uint32_t)>(0x02021a04u)(self,entry); // Contents::Unlink
        at<uint8_t>(self+4)=uint8_t(i);
    }
}
int stage_particle_entry_initialise(uint32_t entry,uint32_t uniqueID,uint32_t definition,
                                    uint32_t position,uint32_t direction,uint32_t callback) {
    // Contents::Entry::Initialise (0x02021d1c).
    const uint32_t data=particle_definition_data(definition);
    at<uint32_t>(data)&=~0x4000u;
    const uint32_t manager=at<uint32_t>(at<uint32_t>(0x0209ee74u)+4);
    const uint32_t system=Ptr<uint32_t(uint32_t,int,uint32_t)>(0x02049e0cu)(manager,int(definition),position);
    at<uint32_t>(entry+0xc)=system;
    if (!system) return 0;
    if (definition==0x52 || definition==0x50) {
        auto &flags=at<uint32_t>(system+0x74); // System::Flags bitfield
        if ((flags&0x3f)<=5 && ((flags>>6)&0x3f)>=5) flags=(flags&~(0x3fu<<12))|(5u<<12);
    }
    at<uint16_t>(entry+8)=at<uint16_t>(data+0x28);
    at<uint16_t>(data+0x28)=0;
    if (direction) std::memcpy(packed::Space::current().data(system+0x3c,6),packed::Space::current().data(direction,6),6);
    if (callback) Ptr<void(uint32_t,uint32_t)>(at<uint32_t>(at<uint32_t>(callback)))(callback,system); // SpawnParticles
    at<uint32_t>(entry)=uniqueID;
    at<uint32_t>(entry+4)=definition;
    at<uint8_t>(entry+0xa)=1;
    at<uint32_t>(entry+0x10)=callback;
    at<uint32_t>(entry+0x14)=at<uint32_t>(entry+0x18)=0;
    return 1;
}
uint32_t stage_particle_create(uint32_t self,uint32_t definition,uint32_t position,uint32_t direction,uint32_t callback) {
    // Contents::Create (0x02021a74).
    for (int i=0;i<0x40;++i) {
        const uint32_t entry=self+8+at<uint8_t>(self+4)*0x1cu;
        if (at<uint32_t>(entry)==0) {
            auto &counter=at<uint32_t>(self);
            if (++counter==0) ++counter;
            if (!stage_particle_entry_initialise(entry,counter,definition,position,direction,callback)) return 0;
            stage_particle_link(self,entry);
            return counter;
        }
        at<uint8_t>(self+4)=uint8_t((at<uint8_t>(self+4)+1)%0x40);
    }
    return 0;
}
void stage_simple_callback_spawn(uint32_t self,uint32_t system) {
    // dPa_c::level_c::simpleCallback_c::SpawnParticles (0x02022640, recovered
    // unit): System::callbackFlags (+0x1c) |= 2, callbackValue (+0x3a) =
    // value (+4), then func_02049d60(data_0209ee74->mManager, &system).
    at<uint32_t>(system+0x1c)|=2;
    at<uint16_t>(system+0x3a)=at<uint16_t>(self+4);
    Ptr<void(uint32_t,uint32_t)>(0x02049d60u)(at<uint32_t>(at<uint32_t>(0x0209ee74u)+4),system);
}
int stage_simple_callback_update(uint32_t self,uint32_t system,int) {
    // dPa_c::level_c::simpleCallback_c::OnUpdate (0x02022630): value (+4) =
    // System::callbackValue (+0x3a).
    at<uint16_t>(self+4)=at<uint16_t>(system+0x3a);
    return 1;
}
uint32_t stage_actor_d2(uint32_t self) {
    // dActor_c D2 (0x020112c8): leave the actor list (stage_actor_destruct).
    stage_actor_destruct(self);
    return self;
}
uint32_t stage_cutscene_object_d1(uint32_t self) {
    // CutsceneObject D1 (ov002 0x020f1f70): vptr 0x0210bd60, dActor_c D2.
    at<uint32_t>(self)=0x0210bd60u;
    return stage_actor_d2(self);
}
uint32_t stage_demo_simple_model_d1(uint32_t self) {
    // daDemo_c::simpleModel_c D1 (ov002 0x020f6a00, cartridge code): its
    // vptr 0x0210bae4, release the model file (+0x5c), Model D2, then the
    // virtual ScaleHolder base (+0x50, 1 x 0xc, Vector3 D1).
    at<uint32_t>(self)=0x0210bae4u;
    if (const uint32_t file=at<uint32_t>(self+0x5c)) Ptr<void(uint32_t)>(0x02017b64u)(file);
    Ptr<void(uint32_t)>(0x02016ca8u)(self);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x0207328cu)(self+0x50,1,0xc,0x020072c0u);
    return self;
}
uint32_t stage_demo_simple_model_d0(uint32_t self) {
    // daDemo_c::simpleModel_c D0 (ov002 0x020f69a8): D1, then operator delete.
    stage_demo_simple_model_d1(self);
    Ptr<void(uint32_t)>(0x0203cbccu)(self);
    return self;
}
uint32_t stage_demo_anm_model_d0(uint32_t self) {
    // daDemo_c::anmModel_c::~anmModel_c() D0 (0x020f6778). The body is
    // external/sm64ds-pc/src/_ZN8daDemo_c10anmModel_cD0Ev.cpp; the structor
    // frame around it is read from the cartridge code: install the class
    // vtables (0x0210bcc4 at +0, 0x0210bce8 at the Animation base +0x50),
    // run the body, dExtAnmModel_c D2 (0x0201689c), __cxa_vec_cleanup of the
    // virtual ScaleHolder base (+0x64, 1 x 0xc, Vector3 D1 0x020072c0), then
    // Memory::operator_delete2 (0x0203cbcc).
    at<uint32_t>(self)=0x0210bcc4u;
    at<uint32_t>(self+0x50)=0x0210bce8u;
    const auto release=[](uint32_t file) { if (file) Ptr<void(uint32_t)>(0x02017b64u)(file); };
    release(at<uint32_t>(self+0x70));
    for (unsigned i=0;i<at<uint8_t>(self+0x80);++i) release(at<uint32_t>(at<uint32_t>(self+0x74)+i*4));
    if (const uint32_t sequence=at<uint32_t>(self+0x7c)) {
        for (unsigned i=0;i<at<uint8_t>(self+0x81);++i) release(at<uint32_t>(at<uint32_t>(self+0x78)+i*4));
        Ptr<void(uint32_t)>(at<uint32_t>(at<uint32_t>(sequence)+4))(sequence);
    }
    Ptr<void(uint32_t)>(0x0201689cu)(self);
    Ptr<void(uint32_t,uint32_t,uint32_t,uint32_t)>(0x0207328cu)(self+0x64,1,0xc,0x020072c0u);
    Ptr<void(uint32_t)>(0x0203cbccu)(self);
    return self;
}
// Structor frames: the recovered units only hold the empty bodies; mwccarm
// emits the vtable stores, the member and base structors in reverse
// declaration order and operator delete. Straight-line frames are generated
// from the cartridge code (structor_frames.py -> stage_structor_frames.inc);
// the ones with a condition are written here.
int stage_sdk_region_table_remove(int type,uint32_t,uint32_t) {
    // func_0206cf98 (SDK, external/sm64ds-pc/src/func_0206cf98.c), reached
    // from FS_EndOverlay: remove an unloaded overlay's range from the SDK
    // region table data_020a9ec0[type]. The native platform never creates
    // that table, so the original's null-table path (return 0) is the only
    // reachable one; a populated table is reported instead of ignored.
    const uint32_t table=unsigned(type)<2 ? at<uint32_t>(0x020a9ec0u+uint32_t(type)*4) : 0;
    if (table) throw std::runtime_error("SDK region table func_0206cf98 is populated: not supported natively.");
    return 0;
}
uint32_t stage_particle_sys_tracker_d1(uint32_t self) {
    // Particle::SysTracker D1 (0x02023194; body external/sm64ds-pc/src/
    // _ZN8Particle10SysTrackerD1Ev.cpp, literals from the cartridge code):
    // clear the contents (+8) and free the shared buffer data_0209ee80, free
    // mResourceFile (+0) unless it is the built-in data_02075f14, then
    // forget the tracker (data_0209ee74).
    if (const uint32_t buffer=at<uint32_t>(0x0209ee80u)) {
        Ptr<void(uint32_t)>(0x02021b98u)(self+8); // SysTracker::Contents::Clear
        Ptr<void(uint32_t)>(0x0203cbc0u)(buffer);
        at<uint32_t>(0x0209ee80u)=0;
    }
    if (at<uint32_t>(self)!=0x02075f14u) Ptr<void(uint32_t)>(0x0203c1b4u)(at<uint32_t>(self));
    at<uint32_t>(0x0209ee74u)=0;
    return self;
}
uint32_t stage_bgw_kc_kcl_file(uint32_t self) {
    // func_01ffb0c8 (ITCM, external/sm64ds-pc/src/func_01ffb0c8.cpp):
    // dBgW_Kc::kclFile (+0x20).
    return at<uint32_t>(self+0x20);
}
uint32_t stage_ext_shadow_model_d1(uint32_t self) {
    // dExtShadowModel_c D1 (0x02015ff8): vptr 0x0208e868, leave the shadow
    // list (data_0209cef4), then ModelBase D2 (0x020170b8).
    at<uint32_t>(self)=0x0208e868u;
    stage_shadow_destruct(self);
    Ptr<uint32_t(uint32_t)>(0x020170b8u)(self);
    return self;
}
void bind_stage_model_runtime(packed::Space &space) {
    // Stage-only scene/message/OAM boundary (the minigame runtime binds its own).
    space.bind(0x0202e1b4u,&scene_set_and_stop_color_fader);
    space.bind(0x0203add4u,&abi_DecIfAbove0_Byte); // DecIfAbove0_Byte
    space.bind(0x0202e1e0u,&scene_set_faders);
    space.bind(0x020440e8u,&stage_list_node_d1);
    space.bind(0x02044104u,&stage_list_node_d1);
    space.bind(0x02015ff8u,&stage_ext_shadow_model_d1);
    space.bind(0x01ffb0c8u,&stage_bgw_kc_kcl_file);
    space.bind(0x02023194u,&stage_particle_sys_tracker_d1);
    space.bind(0x0206cf98u,&stage_sdk_region_table_remove);
    space.bind(0x0201cb7cu,&stage_update_message);
    space.bind(0x0202199cu,&stage_oam_render_sub_palette);
    space.bind(0x02020884u,&stage_oam_load_affine_params);
    space.bind(0x0202e36cu,&scene_set_scene_to_spawn);
    space.bind(0x0202e348u,&scene_start_scene_fade);
    space.bind(0x0202e5f0u,&stage_scene_before_cleanup);
    space.bind(0x0202e5d0u,&stage_scene_after_cleanup);
    space.bind(0x02015cb4u,&stage_frame_ctrl_d2);
    space.bind(0x02015a2cu,&stage_texture_sequence_d1);
    space.bind(0x02015a00u,&stage_texture_sequence_d0);
    space.bind(0x0201689cu,&stage_ext_anm_model_d2);
    space.bind(0x0207328cu,&stage_cxa_vec_cleanup);
    space.bind(0x02016ca8u,&stage_model_d2);
    // Model D1/D0 (0x02016d20/0x02016ce0, cartridge code): D1 repeats the
    // D2 body (which tests transformsBuf), D0 adds operator delete.
    space.bind(0x02016d20u,&stage_model_d2);
    space.bind(0x02016ce0u,&stage_deleting_d0<stage_model_d2>);
    structor_frames::bind_structor_frames(space,-1);
    pmf_state_calls::bind(space,-1);
    space.bind(0x020170b8u,&stage_ext_model_d2);
    space.bind(0x020112c8u,&stage_actor_d2);
    space.bind(0x02022640u,&stage_simple_callback_spawn);
    space.bind(0x02022630u,&stage_simple_callback_update);
    space.bind(0x02021b58u,&stage_particle_find_data);
    space.bind(0x02021cd4u,&stage_particle_entry_reset);
    space.bind(0x02021a48u,&stage_particle_link);
    space.bind(0x02021a04u,&stage_particle_unlink);
    space.bind(0x02021b98u,&stage_particle_contents_clear);
    space.bind(0x02021d1cu,&stage_particle_entry_initialise);
    space.bind(0x02021a74u,&stage_particle_create);
    space.bind(0x02014a2cu,&stage_cc_ac_pos_get_pos);
    space.bind(0x0206df84u,&stage_cstd_abs);
    space.bind(0x02016bb8u,&stage_model_virtual10);
    space.bind(0x0201682cu,&stage_model_anim_virtual10);
}
void bind_stage_audio_runtime(packed::Space &space) {
    // Stage scenes only: the minigame runtime owns its own Sound boundary.
    // A new stage address space starts without dynamic voice bindings.
    stage_voices().fill(StageVoice{});
    stage_seqarc_params().clear();
    space.bind(0x02050cdcu,&stage_seqarc_param);
    space.bind(0x02051fb4u,&stage_snd_start_seq);
    space.bind(0x02051f1cu,&stage_snd_start_seqarc);
    space.bind(0x02051e60u,&stage_snd_start_seqarc_ex);
    space.bind(0x0204f924u,&stage_snd_player_set_volume);
    space.bind(0x0204f7ccu,&stage_snd_player_set_track_pan);
    space.bind(0x0204fa2cu,&stage_snd_stop_seq);
    space.bind(0x0204f8ccu,&stage_snd_move_volume);
    space.bind(0x02012840u,&stage_sound_play_2d);
    space.bind(0x02012790u,&stage_sound_play_2d_archive2);
    space.bind(0x0201264cu,&stage_sound_play_bank0);
    space.bind(0x02012664u,&stage_sound_play_bank3);
    space.bind(0x02012590u,&stage_sound_play_3d);
    space.bind(0x02012328u,&stage_sound_play_long);
    space.bind(0x02012154u,&stage_sound_play_char_voice);
    space.bind(0x0201320cu,&stage_sound_load_and_set_music_layer1);
    space.bind(0x02013050u,&stage_sound_stop_loaded_music_layer1);
}

void bind_stage_runtime(packed::Space &space) {
    bind_stage_tail_veneers(space);
    // The same recovered __sinit_02075154 unit the minigame manifest links.
    space.bind(0x02075154u,&abi___sinit_02075154);
    space.bind(0x02018fa8u,&abi_SetBg3Offset);
    space.bind(0x02015730u,&stage_clipper_ctor);
    space.bind(0x02015560u,&stage_clipper_transform_and_clip);
    space.bind(0x0202e3d4u,&scene_before_behavior);
    space.bind(0x02010fd4u,&actor_before_behavior);
    space.bind(0x02010f78u,&actor_before_render);
    // dActor_c's cleanup overrides only normalise/forward to fBase_c.
    for (uint32_t before : {0x02043bacu,0x02011220u}) space.bind(before,&fbase_before_cleanup);
    for (uint32_t after : {0x02043b2cu,0x02011214u}) space.bind(after,&fbase_after_cleanup);
    space.bind(0x0202e3a4u,&scene_before_render);
    // dActor_c::AfterBehavior/AfterRender only forward to the empty fBase_c ones.
    for (uint32_t after : {0x02043af8u,0x02043ac4u,0x0202e3c8u,0x0202e398u,0x02010fc8u,0x02010f6cu})
        space.bind(after,&fbase_after_process);
    space.bind(0x020431c4u,&scene_waiting_children);
    space.bind(0x0202b710u,&stage_load_fog);
    space.bind(0x0202b534u,&stage_load_texture_transformers);
    space.bind(0x02015950u,&stage_texture_transformer_ctor);
    space.bind(0x02015988u,&stage_texture_sequence_update);
    space.bind(0x0202b0fcu,&stage_load_skybox);
    space.bind(0x0202b8a4u,&stage_render);
    space.bind(0x0202194cu,&stage_oam_render_sub);
    space.bind(0x0202bbbcu,&stage_behavior);
    space.bind(0x0202b268u,&stage_render_model);
    space.bind(0x02015d38u,&stage_shadow_render_all);
    space.bind(0x0202b164u,&stage_render_model_transparent);
    space.bind(0x02022f20u,&stage_particle_tracker_update);
    space.bind(0x02021becu,&stage_particle_contents_update);
    space.bind(0x02022f40u,&stage_particle_tracker_initialise);
    space.bind(0x02023134u,&stage_particle_alloc_tex_vram);
    space.bind(0x02015e14u,&stage_shadow_clean_all);
    space.bind(0x020230ecu,&stage_particle_alloc_pal_vram);
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
    space.bind(0x0203ae58u,&stage_approach_linear_int);
    space.bind(0x0203aed8u,&stage_approach_linear2_short);
    space.bind(0x0200ca50u,&stage_camera_run_behavior);
    space.bind(0x0203adecu,&stage_approach_linear_short);
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
    space.bind(0x0203cbf0u,&stage_deallocate); // operator delete(void *): the default heap
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
    space.bind(0x020733a8u,&stage_vec_ctor);
    space.bind(0x02016da8u,&native_model_base_construct);
    space.bind(0x02016998u,&stage_anim_model_base_ctor);
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
    space.bind(0x02039970u,&stage_mbg_detect_line);
    space.bind(0x0201489cu,&stage_cc_set_pos_relative);
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

namespace sm64ds {
int actor_before_behavior(uint32_t self) {
    // Complete dActor_c::BeforeBehavior (0x02010fd4; recovered isolated unit)
    // with dActor_c.h offsets: mPos 0x5c, mPrevPos 0x68, mCamSpacePos 0x74,
    // mFlags 0xb0, clip offset/radius/distance/far 0xb4..0xc0, result 0xc4,
    // mAreaId 0xcc.
    if (!native_fbase_before_behavior(self)) return 0;
    at<uint32_t>(0x0209b458u)=0;
    auto &flags=at<uint32_t>(self+0xb0);
    const int8_t area=at<int8_t>(self+0xcc);
    if (area>=0 && !Ptr<uint8_t(int)>(0x0202aa6cu)(area)) {
        flags|=0x38;
        for (unsigned lane=0;lane<3;++lane) at<int32_t>(self+0x74+lane*4)=0x7fffffff;
        if (at<int32_t>(0x0209fc68u)==0 || (flags&0x10000)) return 0;
    } else {
        if (at<int32_t>(self+0xb8)!=0) {
            int32_t tmp[3]={at<int32_t>(self+0x5c)>>3,
                            (at<int32_t>(self+0x60)+at<int32_t>(self+0xb4))>>3,
                            at<int32_t>(self+0x64)>>3};
            Ptr<void(Ptr<void>,uint32_t,uint32_t)>(0x02052858u)(Ptr<void>(tmp),0x0209b3ecu,self+0x74);
        }
        if (flags&0x10003) {
            const int32_t r=Ptr<int32_t(uint32_t,uint32_t,int32_t,uint32_t)>(0x020150e8u)(
                0x0209f43cu,self+0x74,at<int32_t>(self+0xb8),self+0xc4);
            int32_t thresh=at<int32_t>(self+0xbc);
            if (at<uint8_t>(0x0209f274u)) thresh<<=1;
            if (r>thresh) {
                flags|=0x18;
                if (flags&0x10000) return 0;
            } else {
                flags&=~0x38u;
                if (r>at<int32_t>(self+0xc0)) flags|=0x10;
            }
        } else {
            flags&=~0x38u;
        }
    }
    bool copy=false;
    if (((at<uint8_t>(0x0209f2c4u)|at<uint8_t>(0x0209f20cu)|at<uint8_t>(0x0209f294u))&0xff)==0) {
        const uint32_t f=flags,mask=at<uint32_t>(0x0209b464u);
        if (((f&9)!=9 && (mask==0 || (f&mask)!=0)) || at<int32_t>(0x0209fc68u)!=0) copy=true;
    }
    if (!copy) return 0;
    for (unsigned lane=0;lane<3;++lane) at<int32_t>(self+0x68+lane*4)=at<int32_t>(self+0x5c+lane*4);
    return 1;
}
// Complete dScene_c::BeforeRender (0x0202e3a4).
int scene_before_render(uint32_t actor) { return native_fbase_before_render(actor) ? 1 : 0; }
int actor_before_render(uint32_t self) {
    // Complete dActor_c::BeforeRender (0x02010f78): skip hidden (0x20) or
    // off-screen-and-culled (8 with 2) actors.
    if (!native_fbase_before_render(self)) return 0;
    const uint32_t flags=at<uint32_t>(self+0xb0);
    if (flags&0x20) return 0;
    return ((flags&8) && (flags&2)) ? 0 : 1;
}
int fbase_before_cleanup(uint32_t self) {
    // Complete fBase_c::BeforeCleanupResources (0x02043bac): wait for the
    // lifecycle state and for every scene child (sceneNode.child, 0x18).
    if (const uint32_t lifecycle=at<uint32_t>(self+0x48))
        if (!Ptr<int(uint32_t)>(0x0204424cu)(lifecycle)) return 0;
    return at<uint32_t>(self+0x18)==0 ? 1 : 0;
}
void fbase_after_cleanup(uint32_t self,uint32_t success) {
    // Complete fBase_c::AfterCleanupResources (0x02043b2c): on success 2,
    // leave the scene tree and cleanup list, destroy the owned heap and
    // lifecycle state, run the virtual D1 (slot 16) and free from the actor
    // heap (fBase_c::operator delete, data_020a0eac).
    if (success!=2) return;
    Ptr<void(uint32_t,uint32_t)>(0x0203b3c0u)(0x020a4b6cu,self+0x14);
    Ptr<void(uint32_t,uint32_t)>(0x0203b27cu)(0x020a4ba8u,self+0x28);
    if (const uint32_t heap=at<uint32_t>(self+0x4c)) Ptr<void(uint32_t)>(0x0203c74cu)(heap);
    if (const uint32_t lifecycle=at<uint32_t>(self+0x48)) Ptr<void(uint32_t)>(0x02044334u)(lifecycle);
    stage_virtual<uint32_t>(self,16);
    packed::Space::current().release(self); // the actor heap is native-owned
}
// fBase_c::AfterBehavior/AfterRender (0x02043af8/0x02043ac4) are empty
// (size 4); dScene_c's overrides (0x0202e3c8/0x0202e398) only forward.
void fbase_after_process(uint32_t,uint32_t) {}
int scene_waiting_children(uint32_t actor) {
    // func_020431c4: walk the scene's descendants, waiting only for an
    // original actor whose aliveState is still pending initialization.
    const uint32_t root=actor+0x14;
    uint32_t node=at<uint32_t>(root+4);
    std::unordered_set<uint32_t> visited;
    while (node) {
        if (!visited.insert(node).second) throw std::runtime_error("Cyclic scene tree.");
        const uint32_t owner=at<uint32_t>(node+16);
        if (!at<uint8_t>(owner+14)) return 1;
        if (at<uint32_t>(node+4)) { node=at<uint32_t>(node+4); continue; }
        while (node!=root && !at<uint32_t>(node+12)) node=at<uint32_t>(node);
        if (!node || node==root) break;
        node=at<uint32_t>(node+12);
    }
    return 0;
}
int scene_before_behavior(uint32_t actor) {
    // Complete dScene_c::BeforeBehavior (0x0202e3d4) over the native fader
    // and scene request owned by GameProgram; shared by stage and minigames.
    if (!native_fbase_before_behavior(actor)) return 0;
    if (at<uint8_t>(0x0209f1e0))
        throw std::runtime_error("Original scene sleep transition is not linked.");
    if (data_02092664!=0x187) {
        if (data_0209f5bc->IsAtStart()) data_0209f5bc->SetForwardTime(30);
        else if (data_0209f5bc->IsAtEnd()) Ptr<void(uint32_t)>(0x02043824)(actor);
        return 1;
    }
    auto &flags=at<uint8_t>(actor+0x13);
    if (flags&1) {
        if (!Ptr<int(uint32_t)>(0x020431c4)(actor)) flags&=static_cast<uint8_t>(~5u);
        return 0;
    }
    if (data_0209f5bc->IsAtEnd()) data_0209f5bc->SetBackwardTime(30);
    return 1;
}
}

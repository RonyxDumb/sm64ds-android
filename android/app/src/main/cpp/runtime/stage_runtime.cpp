#include "stage_runtime.h"
#include "actor_dispatch.h"
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

void bind_stage_runtime(packed::Space &space) {
    space.bind(0x0203cbd8u,&stage_allocate);
    space.bind(0x0203cbc0u,&stage_deallocate);
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

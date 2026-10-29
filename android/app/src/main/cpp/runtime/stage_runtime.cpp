#include "stage_runtime.h"
#include "actor_dispatch.h"
#include "packed_ptr.h"
#include <cstring>
#include <unordered_set>
#include <stdexcept>

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
    space.bind(0x0202e088u,&native_stage_factory);
    space.bind(0x02043decu,&native_fbase_construct);
    space.bind(0x02023204u,&native_particle_tracker_construct);
    space.bind(0x02016d58u,&native_simple_model_construct);
    space.bind(0x02039894u,&native_mesh_collider_construct);
    space.bind(0x020433b8u,&native_actor_post_construct);
    space.bind(0x0202ddc8u,&stage_before_init);
}
}

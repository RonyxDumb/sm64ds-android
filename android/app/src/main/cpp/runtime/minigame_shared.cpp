#include "minigame_shared.h"
#include "packed_ptr.h"
#include "boot_seams.h"
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace sm64ds::minigame_sources {
void bind_sources(packed::Space &);
void bind_overlay_sources(packed::Space &,unsigned);
}
extern "C" { extern int data_0209caa0[]; extern unsigned char gTouchHeld[],gActivePlayerSlot; }
namespace {
// Original ov004 leaf bodies on Nintendo DS-width scene records. Descendant
// overrides still dispatch through their own original callback addresses.
template<class T> T &mg_field(uint32_t address) { return *sm64ds::packed::Ptr<T>(address); }
int mg_base_is_alt(uint32_t actor) {
    return (mg_field<uint32_t>(actor+8u)&0xffu)!=0;
}
int mg_base_display_mode(uint32_t) { return 0; }
void mg_base_pending_destroy(uint32_t) {}
int mg_base_render(uint32_t) { return 1; }
int mg_get_level() {
    const uint32_t scene=mg_field<uint32_t>(0x020beb68u);
    return scene ? static_cast<int>(mg_field<uint32_t>(scene+0xbcu)) : 0;
}
void mg_sub_credits(uint32_t scene,int delta) {
    const int64_t next=int64_t(mg_field<int32_t>(scene+0xa8u))-delta;
    mg_field<int32_t>(scene+0xa8u)=static_cast<int32_t>(std::clamp<int64_t>(next,0,9999));
}
void mg_add_credits(uint32_t scene,int delta) {
    const int64_t next=int64_t(mg_field<int32_t>(scene+0xa8u))+delta;
    mg_field<int32_t>(scene+0xa8u)=static_cast<int32_t>(std::clamp<int64_t>(next,0,9999));
}
void mg_lose_credits(uint32_t amount) {
    const uint32_t scene=mg_field<uint32_t>(0x020beb68u);
    if (scene) mg_sub_credits(scene,static_cast<int32_t>(amount));
}
void mg_gain_credits(uint32_t amount) {
    const uint32_t scene=mg_field<uint32_t>(0x020beb68u);
    if (scene) mg_add_credits(scene,static_cast<int32_t>(amount));
}
}
namespace sm64ds {
void map_minigame_live_arm9(packed::Space &space, Cartridge::DataImage &arm9) {
    // Live aliases are shared with SaveStore and Android's DS touch controller.
    // They must not be replaced by snapshots when overlay 5 gives way to 6.
    struct Live { uint32_t address; void *bytes; size_t size; };
    const Live live[]={{0x0209caa0u,data_0209caa0,0x32c},
                       {0x0209f5f8u,&data_0209f5f8,1},
                       {0x020a0de8u,gTouchHeld,16},
                       {0x020a0e40u,&gActivePlayerSlot,1},
                       {0x020a0e58u,data_020a0e58,16}};
    uint32_t begin=arm9.base;
    for (const auto &span:live) {
        space.map(begin,arm9.bytes.data()+begin-arm9.base,span.address-begin);
        space.map(span.address,span.bytes,span.size);
        begin=span.address+static_cast<uint32_t>(span.size);
    }
    space.map(begin,arm9.bytes.data()+begin-arm9.base,
              arm9.bytes.size()-(begin-arm9.base));
    for (auto &part:arm9.relocated)
        space.map(part.base,part.bytes.data(),part.bytes.size());
}
void bind_minigame_shared_runtime(packed::Space &space) {
    minigame_sources::bind_sources(space);
    minigame_sources::bind_overlay_sources(space,4);
    space.bind(0x020ad660u,&mg_base_is_alt);
    space.bind(0x020b04e0u,&mg_base_display_mode);
    space.bind(0x020b04ecu,&mg_base_pending_destroy);
    space.bind(0x020b04f0u,&mg_base_render);
    space.bind(0x020b1aecu,&mg_get_level);
    space.bind(0x020b1b08u,&mg_lose_credits);
    space.bind(0x020b1b40u,&mg_gain_credits);
    space.bind(0x020b1b78u,&mg_sub_credits);
    space.bind(0x020b1ba0u,&mg_add_credits);
}
void validate_minigame_shared_runtime(const packed::Space &space) {
    for (uint32_t address=0x020bc8bcu;address<0x020bca44u;address+=8) {
        packed::MemberCall call;
        std::memcpy(&call,space.data(address,sizeof call),sizeof call);
        if (call.adjustment!=0 || call.entry<0x020b67e8u || call.entry>=0x020b8714u)
            throw std::runtime_error("Unsupported original minigame state table.");
        (void)space.code(call.entry);
    }
}
}

using sm64ds::packed::Ptr;
using sm64ds::packed::MemberCall;
using sm64ds::packed::Space;
extern int ApproachLinear(int &,int,int);
extern "C" {
void pk_mg_StateInvoke(Ptr<void> object,Ptr<MemberCall> descriptor) {
    MemberCall call;
    std::memcpy(&call,descriptor.get(sizeof call),sizeof call);
    const auto target=Space::current().resolve_member(object.address(),call);
    void (*function)(uint32_t)=nullptr;
    static_assert(sizeof function==sizeof target.code);
    std::memcpy(&function,&target.code,sizeof function);
    function(target.object);
}
int pk_mg_StateSceneCall(Ptr<void> object,int slot,int argument) {
    if (slot!=18 && slot!=19 && slot!=20)
        throw std::out_of_range("Invalid shared minigame scene callback.");
    const auto target=Space::current().resolve_member(object.address(),
        {static_cast<uint32_t>(slot*4),1});
    if (slot==20) {
        void (*function)(uint32_t)=nullptr;
        std::memcpy(&function,&target.code,sizeof function);
        function(target.object);
        return 0;
    }
    if (slot==18) {
        void (*function)(uint32_t,int)=nullptr;
        std::memcpy(&function,&target.code,sizeof function);
        function(target.object,argument);
        return 0;
    }
    int (*function)(uint32_t,int)=nullptr;
    std::memcpy(&function,&target.code,sizeof function);
    return function(target.object,argument);
}
int pk_mg_StateFaderAtStart() { return data_0209f5bc->IsAtStart(); }
int pk_mg_StateFaderAtEnd() { return data_0209f5bc->IsAtEnd(); }
int pk_mg_StateFaderBetween() { return data_0209f5bc->IsBetweenStartAndEnd(); }
int pk_mg_StateFaderForward(unsigned frames,int) { return data_0209f5bc->SetForwardTime(frames); }
int pk_mg_StateFaderBackward(unsigned frames,int) { return data_0209f5bc->SetBackwardTime(frames); }
int pk_mg_StateApproach(Ptr<int> value,int target,int step) {
    return ApproachLinear(*value,target,step);
}
void pk_mg_StateValidate(int state) {
    if (state<0 || state>=20)
        throw std::out_of_range("Invalid original minigame shared state.");
}
}

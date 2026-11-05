#include "minigame_shared.h"
#include "packed_ptr.h"
#include "boot_seams.h"
#include <cstring>
#include <stdexcept>

namespace sm64ds::minigame_sources {
void bind_sources(packed::Space &);
void bind_overlay_sources(packed::Space &,unsigned);
}
namespace sm64ds {
void bind_minigame_shared_runtime(packed::Space &space) {
    minigame_sources::bind_sources(space);
    minigame_sources::bind_overlay_sources(space,4);
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

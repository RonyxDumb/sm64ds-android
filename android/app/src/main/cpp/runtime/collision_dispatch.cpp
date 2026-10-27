#include "collision_dispatch.h"
#include "packed_memory.h"
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <algorithm>
#include <unordered_set>

namespace sm64ds {
namespace {
// fLiNdBa_c-style collision pointers are encoded DS words at +0x28/+0x2c.
// BSS data_0209cee8 is the source's global collision-list head.
constexpr uint32_t kCollisionHead = 0x0209cee8u;
uint32_t get(const packed::Space &space, uint32_t object, uint32_t offset) {
    uint32_t value=0;
    std::memcpy(&value,space.data(object+offset,4),4);
    return value;
}
void put(const packed::Space &space, uint32_t object, uint32_t offset,
         uint32_t value) {
    std::memcpy(space.data(object+offset,4),&value,4);
}
void validate(const packed::Space &space, uint32_t object) {
    (void)space.data(object,0x30);
}
}
namespace {
int32_t signed_word(const packed::Space &space, uint32_t object, uint32_t offset) {
    int32_t v=0;
    std::memcpy(&v,space.data(object+offset,4),4);
    return v;
}
void set_signed(const packed::Space &space, uint32_t object, uint32_t off, int32_t value) {
    std::memcpy(space.data(object+off,4),&value,4);
}
int32_t mul_q12(int32_t a,int32_t b) {
    return static_cast<int32_t>((int64_t(a)*b+0x800ll)>>12);
}
int32_t divide_q12(int32_t a,int32_t b);
using MemberFn=uint32_t (*)(uint32_t);
MemberFn callback(uintptr_t code) {
    MemberFn fn=nullptr;
    static_assert(sizeof fn==sizeof code);
    std::memcpy(&fn,&code,sizeof fn);
    return fn;
}
uint32_t invoke(const packed::Space &space,uint32_t collider,uint32_t slot) {
    const auto method=space.resolve_member(collider,{slot,1});
    return callback(method.code)(method.object);
}
void notify_player(const packed::Space &space,uint32_t owner,uint32_t collider) {
    using Notify=void (*)(uint32_t,uint32_t);
    uintptr_t bits=space.code(0x02014f44u);
    Notify notifier=nullptr;
    static_assert(sizeof notifier==sizeof bits);
    std::memcpy(&notifier,&bits,sizeof notifier);
    notifier(owner,collider);
}
}
namespace cstd {
int sqrt(unsigned long long);
int fdiv(int,int);
}
namespace {
int32_t divide_q12(int32_t a,int32_t b) {return cstd::fdiv(a,b);}
}

void native_collision_init(uint32_t collider, int32_t radius, int32_t height,
                           uint32_t flags, uint32_t vulnerableFlags) {
    auto &space=packed::Space::current();
    validate(space,collider);
    put(space,collider,0x04,static_cast<uint32_t>(radius));
    put(space,collider,0x08,static_cast<uint32_t>(height));
    put(space,collider,0x18,flags);
    put(space,collider,0x1c,vulnerableFlags);
}
void native_collision_clear(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    for(uint32_t off : {0x0cu,0x10u,0x14u,0x20u,0x24u})
        put(space,collider,off,0);
}
void native_collision_update(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    if(get(space,collider,0x18)&1u) return;
    const uint32_t previousHead=get(space,kCollisionHead,0);
    // The caller must first unlink a collider that is already active.
    // Otherwise Nintendo's Update would create a self-cycle.
    if(previousHead==collider ||
       get(space,collider,0x28)!=0 || get(space,collider,0x2c)!=0)
        throw std::runtime_error("Collision Update on an already-linked node.");
    if(previousHead) {
        validate(space,previousHead);
        put(space,previousHead,0x28,collider);
    }
    put(space,collider,0x2c,previousHead);
    put(space,kCollisionHead,0,collider);
}
void native_collision_unlink(uint32_t collider) {
    auto &space=packed::Space::current();
    validate(space,collider);
    const uint32_t previous=get(space,collider,0x28);
    const uint32_t next=get(space,collider,0x2c);
    if(previous) {
        validate(space,previous);
        put(space,previous,0x2c,next);
    } else if(get(space,kCollisionHead,0)==collider) {
        put(space,kCollisionHead,0,next);
    }
    if(next) {
        validate(space,next);
        put(space,next,0x28,previous);
    }
    put(space,collider,0x28,0);
    put(space,collider,0x2c,0);
}
// Recovered dCc_c::Process from src/engine/collision/d_cc.cpp.
// Source's GetPos/GetOwnerID are virtual at DS slots 2/3 (byte offsets
// 8/12). They return packed handles/scalar IDs from registered native code.
// Clear the transient collision list once each processed collider is popped.
void native_collision_process() {
    auto &space=packed::Space::current();
    std::unordered_set<uint32_t> visited;
    while(uint32_t first=get(space,kCollisionHead,0)) {
        if(!visited.insert(first).second || visited.size()>32768u)
            throw std::runtime_error("Cyclic or oversized native collision list.");
        validate(space,first);
        const uint32_t firstPos=invoke(space,first,8);
        (void)space.data(firstPos,12);
        const uint32_t owner=invoke(space,first,12);
        const uint32_t firstFlags=get(space,first,0x18);
        const uint32_t firstVuln=get(space,first,0x1c);
        const int32_t firstRadius=signed_word(space,first,4);
        const int32_t firstHeight=signed_word(space,first,8);
        const int32_t x0=signed_word(space,firstPos,0);
        const int32_t y0=signed_word(space,firstPos,4);
        const int32_t z0=signed_word(space,firstPos,8);
        for(uint32_t other=get(space,first,0x2c);other;) {
            validate(space,other);
            const uint32_t nextOther=get(space,other,0x2c);
            const uint32_t otherFlags=get(space,other,0x18);
            const uint32_t shared=(firstFlags&get(space,other,0x1c)) |
                                  (firstVuln&otherFlags);
            if(shared) {
                const uint32_t otherOwner=invoke(space,other,12);
                if(!owner || owner!=otherOwner) {
                    const uint32_t pos=invoke(space,other,8);
                    (void)space.data(pos,12);
                    const int32_t dy=static_cast<int32_t>(
                        uint32_t(signed_word(space,pos,4))-uint32_t(y0));
                    const int32_t vertical=(dy<0)
                        ? signed_word(space,other,8)+dy : firstHeight-dy;
                    if(vertical>0) {
                        int32_t dx=static_cast<int32_t>(
                            uint32_t(signed_word(space,pos,0))-uint32_t(x0));
                        const int32_t dz=static_cast<int32_t>(
                            uint32_t(signed_word(space,pos,8))-uint32_t(z0));
                        const uint64_t squared=uint64_t(int64_t(dx)*dx)+
                                               uint64_t(int64_t(dz)*dz);
                        int32_t horizontal=cstd::sqrt(squared);
                        if(!horizontal) {horizontal=0x1000;dx=horizontal;}
                        int32_t overlap=firstRadius+signed_word(space,other,4)-horizontal;
                        if(overlap>0) {
                            put(space,first,0x20,otherFlags);
                            put(space,other,0x20,firstFlags);
                            put(space,first,0x24,otherOwner);
                            put(space,other,0x24,owner);
                            bool proceed=true;
                            if(shared&0x4000000u) {
                                put(space,first,0x20,get(space,first,0x20)|0x8000000u);
                                put(space,other,0x20,get(space,other,0x20)|0x8000000u);
                                overlap-=0x50000;
                                proceed=overlap>=0;
                            }
                            if(proceed && !(firstFlags&2u) && !(otherFlags&2u)) {
                                if(firstFlags&4u) {
                                    if(otherFlags&4u) {
                                        const int32_t half=vertical>>1;
                                        set_signed(space,first,0x10,dy<0?half:-half);
                                        set_signed(space,other,0x10,dy<0?-half:half);
                                        for(uint32_t off : {0x0cu,0x14u})
                                            set_signed(space,other,off,0);
                                    } else {
                                        set_signed(space,other,0x10,dy<0?-vertical:vertical);
                                        if(firstFlags&8u) {
                                            const int32_t adjusted=overlap-
                                                (firstRadius-
                                                 static_cast<int32_t>((int64_t(firstRadius)*3)>>3));
                                            overlap=adjusted<0?0:divide_q12(adjusted,horizontal);
                                            notify_player(space,otherOwner,first);
                                        } else overlap=divide_q12(overlap,horizontal);
                                        set_signed(space,other,0x0c,mul_q12(dx,overlap));
                                        set_signed(space,other,0x14,mul_q12(dz,overlap));
                                    }
                                    set_signed(space,first,0x0c,0);
                                    set_signed(space,first,0x14,0);
                                } else if(otherFlags&4u) {
                                    set_signed(space,first,0x10,dy<0?vertical:-vertical);
                                    if(otherFlags&8u) {
                                        const int32_t adjusted=overlap-
                                            (firstRadius-
                                             static_cast<int32_t>((int64_t(firstRadius)*3)>>3));
                                        overlap=adjusted<0?0:divide_q12(adjusted,horizontal);
                                        notify_player(space,owner,other);
                                    } else overlap=divide_q12(overlap,horizontal);
                                    set_signed(space,first,0x0c,-mul_q12(dx,overlap));
                                    set_signed(space,first,0x14,-mul_q12(dz,overlap));
                                    set_signed(space,other,0x0c,0);
                                    set_signed(space,other,0x14,0);
                                } else if(!(firstFlags&0x10000000u) &&
                                          !(otherFlags&0x10000000u)) {
                                    const int32_t half=vertical>>1;
                                    set_signed(space,first,0x10,dy<0?half:-half);
                                    set_signed(space,other,0x10,dy<0?-half:half);
                                    const int32_t ratio=divide_q12(overlap,horizontal)>>1;
                                    set_signed(space,first,0x0c,-mul_q12(dx,ratio));
                                    set_signed(space,first,0x14,-mul_q12(dz,ratio));
                                    set_signed(space,other,0x0c,mul_q12(dx,ratio));
                                    set_signed(space,other,0x14,mul_q12(dz,ratio));
                                }
                            }
                        }
                    }
                }
            }
            other=nextOther;
        }
        const uint32_t next=get(space,first,0x2c);
        put(space,first,0x28,0);
        put(space,first,0x2c,0);
        put(space,kCollisionHead,0,next);
    }
}
} // namespace sm64ds

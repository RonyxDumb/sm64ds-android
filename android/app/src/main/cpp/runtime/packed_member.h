#pragma once
#include "packed_memory.h"
#include "packed_ptr.h"
#include <new>

namespace sm64ds::packed {
struct Vptr {
    uint32_t packed_vptr;
    void packed_set_vptr(uint32_t address) { packed_vptr=address; }
};
// ARM EABI member-function pointers keep the virtual bit in the adjustment
// word. They are data, independent of the host compiler's member-pointer ABI.
template<class T> using StoredArg = std::conditional_t<std::is_reference_v<T>,
    Ptr<std::remove_reference_t<T>>, T>;
template<class T> StoredArg<T> store_arg(T &value) {
    if constexpr (std::is_reference_v<T>) return &value;
    else return value;
}
template<class C, class Signature> struct Member;
template<class C, class R, class... Args> struct Member<C, R(Args...)> {
    uint32_t pointer;
    int32_t adjustment;
    Member() = default;
    constexpr Member(std::nullptr_t) : pointer(0), adjustment(0) {}
    constexpr Member(int zero) : pointer(static_cast<uint32_t>(zero)), adjustment(0) {}
    constexpr Member(uint32_t address, int32_t adjust) : pointer(address), adjustment(adjust) {}
    explicit constexpr operator bool() const { return pointer!=0 || (adjustment&1); }
    bool operator==(int zero) const { return zero==0 && !static_cast<bool>(*this); }
    bool operator!=(int zero) const { return !(*this==zero); }
    template<class T> R call(T object, Args...args) const {
        auto self=Ptr<uint8_t>(object)+(adjustment>>1);
        uint32_t address=pointer;
        if (adjustment&1) address=*Ptr<uint32_t>(*Ptr<uint32_t>(self)+pointer);
        return Ptr<R(Ptr<C>,StoredArg<Args>...)>(address)(Ptr<C>(self),store_arg<Args>(args)...);
    }
};
static_assert(sizeof(Member<int,int()>)==8 && alignof(Member<int,int()>)==4);
static_assert(std::is_trivially_copyable_v<Member<int,int()>>);
template<class T,class C,class R,class... P,class... A>
R member_call(T self,Member<C,R(P...)> function,A&&...args) {
    return function.call(self,std::forward<A>(args)...);
}
template<class Signature,class T,class... Args>
decltype(auto) virtual_call(T object,unsigned slot,Args...args) {
    auto self=Ptr<void>(object);
    auto table=*Ptr<uint32_t>(self);
    return Ptr<Signature>(*Ptr<uint32_t>(table+4*slot))(self,args...);
}
template<class T,class... Args> Ptr<T> make(Args&&...args) {
    auto address=Space::current().allocate(sizeof(T));
    auto object=Ptr<T>(address);
    try { ::new (object.get()) T(std::forward<Args>(args)...); }
    catch (...) { Space::current().release(address); throw; }
    return object;
}
template<class T> void destroy_virtual(T object,unsigned slot) {
    virtual_call<void(Ptr<void>)>(object,slot);
}
}

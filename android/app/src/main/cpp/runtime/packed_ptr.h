#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "ntr/mmio.h"

namespace sm64ds::packed {
// Stored words are handles. All native data/code addresses remain full width
// in Space; a cartridge instruction address is never callable by itself.
void *resolve(uint32_t address, size_t bytes);
// An address-only lookup; it never reads a source output buffer's contents.
uint32_t encode(uintptr_t native_address);
uintptr_t resolve_code(uint32_t address);
uint32_t encode_code(uintptr_t pointer);
template<class T, class = void> struct Extent { static constexpr size_t value = 1; };
template<class T> struct Extent<T, std::void_t<decltype(sizeof(T))>> {
    static constexpr size_t value = sizeof(T);
};
template<class From,class To,class=void> struct BaseConversion : std::false_type {};
template<class From,class To> struct BaseConversion<From,To,std::void_t<decltype(sizeof(From)),decltype(sizeof(To))>>
    : std::bool_constant<std::is_class_v<From> && std::is_class_v<To> && std::is_base_of_v<To,From> &&
                         !std::is_same_v<std::remove_cv_t<From>,std::remove_cv_t<To>>> {};
// Recovered SDK code also keeps a volatile register pointer in a local.
// Those writes must reach the same command/arithmetic ports as literal MMIO.
template<class T> class VolatileRef {
    uint32_t address_;
    using Value = std::remove_cv_t<T>;
    bool port() const { return address_>=ntr::IO_BASE && address_<ntr::IO_BASE+ntr::IO_SIZE; }
public:
    explicit VolatileRef(uint32_t address) : address_(address) {}
    VolatileRef(const VolatileRef &) = default;
    operator Value() const {
        if (port()) return static_cast<Value>(ntr::io_read(address_,sizeof(Value)));
        if constexpr (std::is_class_v<Value>) {
            static_assert(sizeof(Value)==4 && std::is_trivially_copyable_v<Value>);
            return Value(*static_cast<volatile uint32_t *>(resolve(address_,4)));
        } else return *static_cast<T *>(resolve(address_,sizeof(Value)));
    }
    VolatileRef &operator=(Value value) {
        if (port()) ntr::io_write(address_,static_cast<uint64_t>(value),sizeof(Value));
        else if constexpr (std::is_class_v<Value>) {
            static_assert(sizeof(Value)==4 && std::is_trivially_copyable_v<Value>);
            uint32_t bits; std::memcpy(&bits,&value,4);
            *static_cast<volatile uint32_t *>(resolve(address_,4))=bits;
        } else *static_cast<T *>(resolve(address_,sizeof(Value)))=value;
        return *this;
    }
    VolatileRef &operator=(const VolatileRef &other) { return *this=static_cast<Value>(other); }
    template<class V=Value> auto operator->() const -> decltype(std::declval<V>().operator->()) {
        return static_cast<Value>(*this).operator->();
    }
    template<class U> auto operator+(U value) const { return static_cast<Value>(*this)+value; }
    template<class U> auto operator-(U value) const { return static_cast<Value>(*this)-value; }
    template<class U> VolatileRef &operator|=(U value) { return *this=static_cast<Value>(*this)|value; }
    template<class U> VolatileRef &operator&=(U value) { return *this=static_cast<Value>(*this)&value; }
};
template<class T> class Ptr {
    uint32_t address_;
public:
    Ptr() = default;
    constexpr Ptr(std::nullptr_t) : address_(0) {}
    template<class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    constexpr Ptr(I value) : address_(static_cast<uint32_t>(value)) {}
    template<class U> constexpr Ptr(Ptr<U> value) : address_(value.address()) {
        if constexpr (BaseConversion<U,T>::value)
            if (address_) address_=encode(reinterpret_cast<uintptr_t>(static_cast<T *>(value.get())));
    }
    template<class U> Ptr(U *value) {
        if constexpr (std::is_function_v<U>) {
            uintptr_t bits = 0;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&bits, &value, sizeof bits);
            address_ = value ? encode_code(bits) : 0;
        } else {
            static_assert(sizeof(uintptr_t)>=sizeof(value));
            address_ = value ? encode(reinterpret_cast<uintptr_t>(value)) : 0;
        }
    }
    constexpr uint32_t address() const { return address_; }
    uint32_t address() const volatile { return address_; }
    template<class U> void operator=(Ptr<U> value) volatile { address_=Ptr(value).address(); }
    template<class U> Ptr &operator=(Ptr<U> value) { address_=Ptr(value).address(); return *this; }
    constexpr operator int32_t() const { return static_cast<int32_t>(address_); }
    T *get(size_t bytes = Extent<T>::value) const {
        static_assert(!std::is_function_v<T>, "use the bound native call operator");
        if constexpr (std::is_class_v<T>)
            static_assert(!std::is_polymorphic_v<T>, "project the actor ABI before using a native C++ vptr in a packed record");
        return static_cast<T *>(resolve(address_, bytes));
    }
    T *operator->() const { return get(); }
    template<class U=T, std::enable_if_t<!std::is_void_v<U> && !std::is_function_v<U>, int> = 0>
    decltype(auto) operator*() const {
        if constexpr (std::is_volatile_v<U>) return VolatileRef<U>(address_);
        else return *get();
    }
    template<class U=T, std::enable_if_t<!std::is_void_v<U> && !std::is_function_v<U>, int> = 0>
    decltype(auto) operator[](ptrdiff_t index) const { return *(*this + index); }
    template<class U> bool operator==(U *other) const { return address_==Ptr<U>(other).address(); }
    template<class U> bool operator!=(U *other) const { return !(*this==other); }
    template<class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    Ptr operator+(I count) const { return Ptr(address_ + uint32_t(count * Extent<T>::value)); }
    template<class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    Ptr operator-(I count) const { return Ptr(address_ - uint32_t(count * Extent<T>::value)); }
    template<class I> Ptr &operator+=(I count) { return *this = *this + count; }
    template<class I> Ptr &operator-=(I count) { return *this = *this - count; }
    Ptr &operator++() { return *this += 1; }
    Ptr operator++(int) { auto old=*this; ++*this; return old; }
    Ptr &operator--() { return *this -= 1; }
    Ptr operator--(int) { auto old=*this; --*this; return old; }
    template<class U> ptrdiff_t operator-(Ptr<U> other) const {
        return int64_t(int32_t(address_ - other.address())) / ptrdiff_t(Extent<T>::value);
    }
    template<class... Args, class U=T, std::enable_if_t<std::is_function_v<U>, int> = 0>
    decltype(auto) operator()(Args...args) const {
        auto bits = resolve_code(address_);
        U *function = nullptr;
        static_assert(sizeof(function) == sizeof bits);
        std::memcpy(&function, &bits, sizeof function);
        return function(args...);
    }
};
static_assert(sizeof(Ptr<void>) == 4 && alignof(Ptr<void>) == 4);
static_assert(std::is_trivially_copyable_v<Ptr<void>>);
// A declared array denotes an address without first forming a T[] reference.
// Recovered declarations sometimes give a byte alias (even an odd address)
// a wider element type and cast it before the first access.
template<class T> class ArrayRef : public Ptr<T> {
public:
    explicit constexpr ArrayRef(uint32_t address) : Ptr<T>(address) {}
    Ptr<T[]> operator&() const { return Ptr<T[]>(this->address()); }
};
static_assert(sizeof(ArrayRef<int>)==4);
template<class T> uint32_t uword(T value) {
    if constexpr (std::is_pointer_v<T>) return Ptr<std::remove_pointer_t<T>>(value).address();
    else return static_cast<uint32_t>(value);
}
template<class T> int32_t word(T value) { return static_cast<int32_t>(uword(value)); }
}

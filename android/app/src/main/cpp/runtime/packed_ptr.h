#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace sm64ds::packed {
// Stored words are handles. All native data/code addresses remain full width
// in Space; a cartridge instruction address is never callable by itself.
void *resolve(uint32_t address, size_t bytes);
uint32_t encode(const void *pointer);
uintptr_t resolve_code(uint32_t address);
uint32_t encode_code(uintptr_t pointer);
template<class T, class = void> struct Extent { static constexpr size_t value = 1; };
template<class T> struct Extent<T, std::void_t<decltype(sizeof(T))>> {
    static constexpr size_t value = sizeof(T);
};
template<class T> class Ptr {
    uint32_t address_;
public:
    Ptr() = default;
    constexpr Ptr(std::nullptr_t) : address_(0) {}
    template<class I, std::enable_if_t<std::is_integral_v<I>, int> = 0>
    constexpr Ptr(I value) : address_(static_cast<uint32_t>(value)) {}
    template<class U> constexpr Ptr(Ptr<U> value) : address_(value.address()) {}
    template<class U> Ptr(U *value) {
        if constexpr (std::is_function_v<U>) {
            uintptr_t bits = 0;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&bits, &value, sizeof bits);
            address_ = value ? encode_code(bits) : 0;
        } else {
            address_ = value ? encode(const_cast<const void *>(static_cast<const volatile void *>(value))) : 0;
        }
    }
    constexpr uint32_t address() const { return address_; }
    constexpr operator int32_t() const { return static_cast<int32_t>(address_); }
    T *get(size_t bytes = Extent<T>::value) const {
        static_assert(!std::is_function_v<T>, "use the bound native call operator");
        if constexpr (std::is_class_v<T>)
            static_assert(!std::is_polymorphic_v<T>, "project the actor ABI before using a native C++ vptr in a packed record");
        return static_cast<T *>(resolve(address_, bytes));
    }
    T *operator->() const { return get(); }
    template<class U=T, std::enable_if_t<!std::is_void_v<U> && !std::is_function_v<U>, int> = 0>
    U &operator*() const { return *get(); }
    template<class U=T, std::enable_if_t<!std::is_void_v<U> && !std::is_function_v<U>, int> = 0>
    U &operator[](ptrdiff_t index) const { return *((*this + index).get()); }
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
    decltype(auto) operator()(Args &&...args) const {
        auto bits = resolve_code(address_);
        U *function = nullptr;
        static_assert(sizeof(function) == sizeof bits);
        std::memcpy(&function, &bits, sizeof function);
        return function(std::forward<Args>(args)...);
    }
};
static_assert(sizeof(Ptr<void>) == 4 && alignof(Ptr<void>) == 4);
static_assert(std::is_trivially_copyable_v<Ptr<void>>);
template<class T> uint32_t uword(T value) {
    if constexpr (std::is_pointer_v<T>) return Ptr<std::remove_pointer_t<T>>(value).address();
    else return static_cast<uint32_t>(value);
}
template<class T> int32_t word(T value) { return static_cast<int32_t>(uword(value)); }
}

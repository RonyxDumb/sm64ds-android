#pragma once
#include "packed_ptr.h"

namespace sm64ds::native_abi {
// Address-valued DS integers are handles in the native process. Encode data
// and callbacks separately; neither conversion may truncate an ARM64 address.
using packed::word;
using packed::uword;

template<class T, class U> T pointer(U value) {
    static_assert(std::is_pointer_v<T>);
    if constexpr (std::is_integral_v<U> || std::is_enum_v<U>) {
        const auto address = static_cast<uint32_t>(value);
        if (!address) return nullptr;
        if constexpr (std::is_function_v<std::remove_pointer_t<T>>) {
            const uintptr_t code = packed::resolve_code(address);
            T result = nullptr;
            static_assert(sizeof result == sizeof code);
            std::memcpy(&result, &code, sizeof result);
            return result;
        } else {
            return static_cast<T>(packed::resolve(address,
                packed::Extent<std::remove_pointer_t<T>>::value));
        }
    } else {
        // Retain the original C++ pointer conversion, including any base-class
        // adjustment. No DS handle is involved in a native-to-native cast.
        return (T)value;
    }
}

// A data pointer stored inside a cartridge-layout POD record. Ordinary native
// pointers remain full width; only the stored representation occupies 4 bytes.
template<class T> class StoredPointer {
    uint32_t address_;
public:
    StoredPointer() = default;
    StoredPointer(T *value) : address_(packed::uword(value)) {}
    StoredPointer &operator=(T *value) {
        address_ = packed::uword(value);
        return *this;
    }
    operator T *() const { return pointer<T *>(address_); }
    T *operator->() const { return pointer<T *>(address_); }
    template<class I, class U=T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U &operator[](I index) const { return pointer<U *>(address_)[index]; }
    template<class U=T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U &operator*() const { return *pointer<U *>(address_); }
    template<class U> explicit operator U *() const { return pointer<U *>(address_); }
    explicit operator uint32_t() const { return address_; }
};
static_assert(sizeof(StoredPointer<void>) == 4);
static_assert(alignof(StoredPointer<void>) == 4);
static_assert(std::is_trivially_copyable_v<StoredPointer<void>>);
}

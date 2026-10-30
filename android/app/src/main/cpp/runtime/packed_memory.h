#pragma once
#include "game_heap.h"
#include <cstring>
#include <memory>
#include <vector>

namespace sm64ds::packed {
// ARM/Itanium pointer-to-member-function representation used by the DS:
// a code handle or virtual-table byte offset, followed by a signed
// this-adjustment whose low bit selects virtual dispatch.
struct MemberCall {
    uint32_t entry;
    int32_t adjustment;
};
static_assert(sizeof(MemberCall) == 8);
struct MemberTarget {
    uint32_t object; // adjusted packed handle, never a truncated host pointer
    uintptr_t code;  // resolved native function address
};

class Space {
public:
    explicit Space(GameHeap &heap);
    ~Space();
    Space(const Space &) = delete;
    Space &operator=(const Space &) = delete;
    void map(uint32_t address, void *memory, size_t size);
    void map_owned(uint32_t address, size_t size);
    bool contains(uint32_t address, size_t bytes) const;
    void *data(uint32_t address, size_t bytes) const;
    uint32_t address(const void *pointer);
    uint32_t allocate(uint32_t bytes, int alignment = 4);
    uint32_t allocation_size(uint32_t address) const;
    uint32_t resize(uint32_t address,uint32_t bytes);
    void release(uint32_t address);
    size_t allocations() const { return allocations_.size(); }
    size_t largest_free_span() const;
    void bind_code(uint32_t address, uintptr_t native);
    template<class T> void bind(uint32_t address, T *function) {
        uintptr_t bits = 0;
        static_assert(sizeof(bits) == sizeof(function));
        std::memcpy(&bits, &function, sizeof bits);
        bind_code(address, bits);
    }
    uintptr_t code(uint32_t address) const;
    // Resolve original DS virtual/member calls through checked 32-bit
    // records and the native function table. Never execute guest code.
    MemberTarget resolve_member(uint32_t object, MemberCall member) const;
    uint32_t code_address(uintptr_t native);
    static Space &current();
    class Scope {
        Space *previous_;
    public:
        explicit Scope(Space &space);
        ~Scope();
    };
private:
    struct Region { uint32_t address; size_t size; uint8_t *memory; };
    GameHeap &heap_;
    std::vector<Region> regions_;
    std::vector<std::unique_ptr<uint8_t[]>> ownedRegions_;
    // Hot reverse lookup for native-to-DS pointer encoding. The frontend
    // repeatedly visits fields in the same mapped stack/heap/overlay region.
    Region recentNative_{};
    bool recentNativeValid_ = false;
    std::map<uint32_t, void *> allocations_;
    std::map<uint32_t, uintptr_t> code_;
    uint32_t external_ = 0x30000000, nextCode_ = 0x60000000;
    uint32_t nextStack_ = 0x70000000;
    void map_thread_stack();
    bool map_native_segment(const void *pointer);
};
}

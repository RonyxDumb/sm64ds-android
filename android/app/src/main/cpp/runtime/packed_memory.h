#pragma once
#include "game_heap.h"
#include <cstring>
#include <vector>

namespace sm64ds::packed {
class Space {
public:
    explicit Space(GameHeap &heap);
    ~Space();
    Space(const Space &) = delete;
    Space &operator=(const Space &) = delete;
    void map(uint32_t address, void *memory, size_t size);
    void *data(uint32_t address, size_t bytes) const;
    uint32_t address(const void *pointer);
    uint32_t allocate(uint32_t bytes);
    uint32_t allocation_size(uint32_t address) const;
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
    std::map<uint32_t, void *> allocations_;
    std::map<uint32_t, uintptr_t> code_;
    uint32_t external_ = 0x30000000, nextCode_ = 0x60000000;
    bool map_native_segment(const void *pointer);
};
}

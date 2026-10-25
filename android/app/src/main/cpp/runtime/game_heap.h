#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

namespace sm64ds {
// Native heap ownership. Metadata and native pointers live in host objects;
// cartridge structures keep their original 32-bit relative offsets untouched.
class GameHeap {
public:
    explicit GameHeap(size_t capacity = 32u * 1024 * 1024);
    ~GameHeap();
    GameHeap(const GameHeap &) = delete;
    GameHeap &operator=(const GameHeap &) = delete;
    void *allocate(uint32_t size, int alignment = 4);
    void deallocate(void *pointer);
    uint32_t resize(void *pointer, uint32_t size);
    uint32_t size_of(const void *pointer) const;
    size_t memory_left() const;
    size_t allocations() const { return used_.size(); }
    uint32_t offset_of(const void *pointer) const;
    void *at(uint32_t offset, uint32_t size) const;

private:
    uint8_t *base_;
    size_t capacity_;
    std::map<size_t, size_t> free_, used_;
    void release_span(size_t offset, size_t size);
};
}

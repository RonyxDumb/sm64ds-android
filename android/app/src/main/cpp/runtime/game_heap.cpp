#include "game_heap.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>

namespace sm64ds {
GameHeap::GameHeap(size_t capacity) : base_(nullptr), capacity_(capacity) {
    if (capacity < 64 || capacity > 512u * 1024 * 1024)
        throw std::invalid_argument("Invalid native game heap capacity.");
    void *mapping = mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
        throw std::bad_alloc();
    base_ = static_cast<uint8_t *>(mapping);
    try { free_.emplace(0, capacity); }
    catch (...) { munmap(base_, capacity_); throw; }
}
GameHeap::~GameHeap() { munmap(base_, capacity_); }
void *GameHeap::allocate(uint32_t requested, int alignment) {
    // The recovered allocator uses the alignment's sign to choose the end of
    // the arena. Keep that contract, with all address arithmetic at host width.
    const int64_t magnitude = alignment < 0 ? -int64_t(alignment) : alignment;
    if (magnitude < 1 || magnitude > 65536 || (magnitude & (magnitude - 1)))
        throw std::invalid_argument("Game heap alignment must be a signed power of two.");
    const uintptr_t mask = static_cast<uintptr_t>(magnitude - 1);
    const size_t size = (std::max<uint64_t>(requested, 1) + 3) & ~uint64_t(3);
    if (size > capacity_)
        return nullptr;
    const uintptr_t base = reinterpret_cast<uintptr_t>(base_);
    auto reserve = [&](auto it) -> void * {
        const size_t low = it->first, high = low + it->second;
        if (size > it->second)
            return nullptr;
        const uintptr_t address = alignment > 0 ? (base + low + mask) & ~mask
                                              : (base + high - size) & ~mask;
        if (address < base + low || address - base > high - size)
            return nullptr;
        const size_t offset = static_cast<size_t>(address - base);
        // Keep bookkeeping external to the arena. In particular no host pointer
        // is ever stored in the game's 0x10-byte DS MemoryNode layout.
        used_.emplace(offset, size);
        free_.erase(it);
        if (offset > low) free_.emplace(low, offset - low);
        if (offset + size < high) free_.emplace(offset + size, high - offset - size);
        return base_ + offset;
    };
    if (alignment > 0) {
        for (auto it = free_.begin(); it != free_.end(); ++it)
            if (void *p = reserve(it)) return p;
    } else {
        for (auto it = free_.rbegin(); it != free_.rend(); ++it)
            if (void *p = reserve(std::prev(it.base()))) return p;
    }
    return nullptr;
}
uint32_t GameHeap::offset_of(const void *pointer) const {
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t base = reinterpret_cast<uintptr_t>(base_);
    if (address < base || address - base >= capacity_)
        throw std::invalid_argument("Pointer does not belong to the native game heap.");
    return static_cast<uint32_t>(address - base);
}
void *GameHeap::at(uint32_t offset, uint32_t size) const {
    if (offset > capacity_ || size > capacity_ - offset)
        throw std::out_of_range("Game heap offset is outside its arena.");
    return base_ + offset;
}
void GameHeap::release_span(size_t offset, size_t size) {
    auto next = free_.lower_bound(offset);
    if (next != free_.end() && offset + size == next->first) {
        size += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin()) {
        auto before = std::prev(next);
        if (before->first + before->second == offset) {
            offset = before->first;
            size += before->second;
            free_.erase(before);
        }
    }
    free_.emplace(offset, size);
}
void GameHeap::deallocate(void *pointer) {
    if (!pointer) return;
    const size_t offset = offset_of(pointer);
    const auto it = used_.find(offset);
    if (it == used_.end())
        throw std::invalid_argument("Game heap invalid or duplicate free.");
    const size_t size = it->second;
    used_.erase(it);
    release_span(offset, size);
}
uint32_t GameHeap::size_of(const void *pointer) const {
    const auto it = used_.find(offset_of(pointer));
    if (it == used_.end())
        throw std::invalid_argument("Game heap pointer is not an allocation.");
    return static_cast<uint32_t>(it->second);
}
uint32_t GameHeap::resize(void *pointer, uint32_t requested) {
    const size_t offset = offset_of(pointer);
    auto it = used_.find(offset);
    if (it == used_.end())
        throw std::invalid_argument("Game heap resize is not an allocation.");
    const size_t size = (std::max<uint64_t>(requested, 1) + 3) & ~uint64_t(3);
    const size_t old = it->second;
    if (size > old) {
        const auto next = free_.find(offset + old);
        if (next == free_.end() || size - old > next->second) return 0;
        const size_t remain = next->second - (size - old);
        free_.erase(next);
        if (remain) free_.emplace(offset + size, remain);
    } else if (size < old) {
        release_span(offset + size, old - size);
    }
    it->second = size;
    return static_cast<uint32_t>(size);
}
size_t GameHeap::memory_left() const {
    size_t count = 0;
    for (const auto &block : free_) count += block.second;
    return count;
}
}

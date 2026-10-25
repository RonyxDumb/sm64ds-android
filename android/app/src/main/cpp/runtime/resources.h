#pragma once
#include "game_heap.h"
#include <cstdint>
#include <vector>

namespace sm64ds {
std::vector<uint8_t> unpack_overlay(const std::vector<uint8_t> &packed, uint32_t ramSize,
                                    bool compressed);
// A single mounted cartridge and native game heap, leased for the lifetime of
// a session. SharedFilePtr's actual recovered refcount code uses this seam.
class Resources {
public:
    explicit Resources(const char *path);
    ~Resources();
    Resources(const Resources &) = delete;
    Resources &operator=(const Resources &) = delete;
    uint16_t file_id(uint32_t handle) const;
    uint32_t handle_by_path(const char *path) const;
    std::vector<uint8_t> load(const char *path);
    GameHeap &heap() { return heap_; }
    void remember(void *pointer, uint32_t bytes) { sizes_.emplace(pointer, bytes); }
    void forget(void *pointer) { sizes_.erase(pointer); }
    static Resources &active();
    size_t handle_count() const { return files_.size(); }

private:
    GameHeap heap_;
    std::vector<uint16_t> files_;
    std::map<void *, uint32_t> sizes_;
};
}

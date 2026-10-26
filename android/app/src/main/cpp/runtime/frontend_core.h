#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include <array>

namespace sm64ds {
// Original frontend core records; this is not a substitute for the unfinished
// menu dispatcher, display builders, or game scene.
class FrontendCore {
public:
    FrontendCore(GameHeap &heap, Cartridge::DataImage data, int ownerLanguage);
    ~FrontendCore();
    void sample(uint16_t keys, int x, int y);
    void step_state(unsigned index);
    uint32_t root_address() const { return root_; }
    const void *data_at(uint32_t address, size_t bytes) const { return space_.data(address,bytes); }
    uint32_t word_at(uint32_t address) const;
    size_t allocations() const { return space_.allocations(); }
    int language() const { return ownerLanguage_; }
    void latch_touch() { latchedTouch_=touch_; }
    const std::array<uint16_t,4> &latched_touch() const { return latchedTouch_; }
    static FrontendCore &current();
    class Scope {
        packed::Space::Scope address_;
        FrontendCore *previous_;
    public:
        explicit Scope(FrontendCore &core);
        ~Scope();
    };
private:
    packed::Space space_;
    Cartridge::DataImage image_;
    uint32_t root_=0;
    int ownerLanguage_;
    std::array<uint16_t,4> touch_{}, latchedTouch_{};
};
}

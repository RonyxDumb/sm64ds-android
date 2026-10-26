#pragma once
#include "cartridge.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm64ds {

// The original level-to-overlay and LVL_Overlay records are 32-bit cartridge
// data. StageAssets owns their exact bytes; no DS pointer is ever cast into a
// native C++ pointer, and no game logic or substitute player is synthesized.
class StageAssets {
public:
    struct Descriptor {
        uint32_t level = 0, overlayId = 0, levelAddress = 0;
        uint32_t clpsAddress = 0, objectsAddress = 0, subTablesAddress = 0;
        uint16_t modelHandle = 0, collisionHandle = 0;
        uint16_t minimapCharacters = 0, minimapPalette = 0;
        uint8_t subTableCount = 0, flags = 0;
    };

    static StageAssets load(Cartridge &cartridge, uint32_t sublevel);
    const Descriptor &descriptor() const { return descriptor_; }
    const std::vector<uint8_t> &model() const { return model_; }
    const std::vector<uint8_t> &collision() const { return collision_; }
    Cartridge::DataImage &overlay() { return overlay_; }
    const Cartridge::DataImage &overlay() const { return overlay_; }

    // Return a checked span of the unmodified level overlay image. This is
    // the source of truth for the original actor's object and CLPS tables.
    const uint8_t *overlay_at(uint32_t guest, size_t count) const;

private:
    Descriptor descriptor_;
    Cartridge::DataImage overlay_{};
    std::vector<uint8_t> model_, collision_;
};

} // namespace sm64ds

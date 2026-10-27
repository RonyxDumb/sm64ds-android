#pragma once
#include "cartridge.h"
#include <array>
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

    // A ROM object table has an 8-byte header (u16 count, u32 entry pointer)
    // and 8-byte entry records. The low five bits of the first entry byte
    // select the original actor loader, the upper three filter by star.
    struct ObjectRecord {
        std::array<uint8_t, 8> data{};
        uint32_t guestAddress = 0;
        int area = -1; // -1 = global objects, otherwise the 0-based area
        uint8_t type = 0, starFilter = 0, entryCount = 0;
        uint32_t entriesAddress = 0;
        uint32_t loaderAddress = 0; // original ov002 loader, never a host code pointer

        bool selected_for(uint8_t star) const {
            return starFilter == 0 || starFilter == star;
        }
    };

    // Recovered LoadSimpleObjects calls dActor_c::Spawn for 9-bit actor
    // indices. This is its native, pointer-free *request*, not a spawned actor.
    struct SimpleSpawn {
        uint16_t actorId = 0, actorIndex = 0, param = 0;
        int32_t x = 0, y = 0, z = 0; // original Q12 world coordinates
        int area = -1;
        bool minimapChange = false;
    };

    static StageAssets load(Cartridge &cartridge, uint32_t sublevel);
    static uint32_t original_loader(const Cartridge::DataImage &objectOverlay, uint8_t type);
    static std::vector<SimpleSpawn> decode_simple_spawns(
        const Cartridge::DataImage &levelOverlay,
        const Cartridge::DataImage &objectOverlay, const ObjectRecord &category);
    const std::vector<SimpleSpawn> &simple_spawns() const { return simpleSpawns_; }
    const Cartridge::DataImage &object_overlay() const { return objectOverlay_; }
    // Byte-preserving projection of original LoadObjects/LoadClsnAndObjects
    // records. Never follows guest addresses as host pointers.
    static std::vector<ObjectRecord> decode_object_catalog(
        const Cartridge::DataImage &overlay, const Descriptor &descriptor);
    const std::vector<ObjectRecord> &objects() const { return objects_; }
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
    Cartridge::DataImage overlay_{}, objectOverlay_{};
    std::vector<uint8_t> model_, collision_;
    std::vector<ObjectRecord> objects_;
    std::vector<SimpleSpawn> simpleSpawns_;
};

} // namespace sm64ds

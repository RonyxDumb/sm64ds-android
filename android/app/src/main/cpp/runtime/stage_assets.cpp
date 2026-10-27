#include "stage_assets.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
uint16_t half(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t word(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
const uint8_t *guest_data(const sm64ds::Cartridge::DataImage &image,
                          uint32_t address, size_t bytes) {
    if (address < image.base || uint64_t(address) - image.base > image.bytes.size() ||
        bytes > image.bytes.size() - (uint64_t(address) - image.base))
        throw std::runtime_error("Recovered stage address outside its cartridge overlay.");
    return image.bytes.data() + (address - image.base);
}
}

namespace sm64ds {

// The ROM's ov002 data table has fifteen four-byte ARM handler addresses.
// They are used only to identify recovered functions; calling a guest
// address as an ARM64 function pointer would be invalid.
uint32_t StageAssets::original_loader(const Cartridge::DataImage &objectOverlay, uint8_t type) {
    if (type >= 15)
        throw std::runtime_error("Level object category exceeds original 15-entry dispatch table.");
    return word(guest_data(objectOverlay, 0x0210cbb8u + uint32_t(type) * 4, 4));
}

// Source: src/game/stages/LevelObjects.cpp::LoadSimpleObjects.
// The actual actor table is a u16 array in ov002 at 0x0210cbf4.
// This prepares exact values for the later dActor_c::Spawn bridge; it does not
// fabricate Player objects, animation or collision.
std::vector<StageAssets::SimpleSpawn> StageAssets::decode_simple_spawns(
    const Cartridge::DataImage &levelOverlay, const Cartridge::DataImage &objectOverlay,
    const ObjectRecord &category) {
    require(category.loaderAddress == 0x020fe960u,
            "Category is not handled by original LoadSimpleObjects.");
    std::vector<SimpleSpawn> spawns;
    if (!category.entryCount)
        return spawns;
    const uint8_t *entries = guest_data(levelOverlay, category.entriesAddress,
                                         size_t(category.entryCount) * 8);
    spawns.reserve(category.entryCount);
    for (unsigned i = 0; i < category.entryCount; ++i) {
        const auto *entry = entries + size_t(i) * 8;
        const uint16_t raw = half(entry);
        SimpleSpawn spawn{};
        spawn.actorIndex = raw & 0x1ffu;
        spawn.param = raw >> 9;
        // The original uses a signed s16-to-Q12 conversion. Multiplication
        // avoids undefined behavior from left-shifting a negative integer.
        spawn.x = int32_t(static_cast<int16_t>(half(entry + 2))) * 4096;
        spawn.y = int32_t(static_cast<int16_t>(half(entry + 4))) * 4096;
        spawn.z = int32_t(static_cast<int16_t>(half(entry + 6))) * 4096;
        spawn.area = category.area;
        spawn.minimapChange = spawn.actorIndex == 0x1ff;
        if (spawn.minimapChange)
            spawn.param &= 0xf; // special native minimap-change argument
        else
            spawn.actorId = half(guest_data(objectOverlay,
                                     0x0210cbf4u + uint32_t(spawn.actorIndex) * 2, 2));
        spawns.push_back(spawn);
    }
    return spawns;
}

// LoadObjects (LevelObjects.cpp) reads a u16 count at +0 and a 32-bit
// pointer at +4; each record is exactly eight bytes. Area table records
// have a 12-byte stride and contain another 32-bit table pointer at +0.
// These are ROM records, not native LVL_Overlay C++ objects.
std::vector<StageAssets::ObjectRecord> StageAssets::decode_object_catalog(
    const Cartridge::DataImage &overlay, const Descriptor &descriptor) {
    constexpr size_t kObjectLimit = 32768;
    constexpr unsigned kAreaLimit = 127;
    const auto at = [&](uint32_t address, size_t size) -> const uint8_t * {
        if (address < overlay.base ||
            uint64_t(address) - overlay.base > overlay.bytes.size() ||
            size > overlay.bytes.size() - (uint64_t(address) - overlay.base))
            throw std::runtime_error("Stage object table points outside its native overlay.");
        return overlay.bytes.data() + (address - overlay.base);
    };
    std::vector<ObjectRecord> result;
    const auto readTable = [&](uint32_t address, int area) {
        const uint8_t *table = at(address, 8);
        const unsigned count = half(table);
        const uint32_t entries = word(table + 4);
        if (count == 0)
            return;
        if (count > kObjectLimit - result.size())
            throw std::runtime_error("Stage object catalog exceeds native actor limit.");
        const uint8_t *bytes = at(entries, size_t(count) * 8);
        for (unsigned i = 0; i < count; ++i) {
            const uint8_t *entry = bytes + size_t(i) * 8;
            ObjectRecord object{};
            std::copy_n(entry, object.data.size(), object.data.begin());
            object.guestAddress = entries + i * 8;
            object.area = area;
            object.type = entry[0] & 0x1f;
            object.starFilter = (entry[0] >> 5) & 7;
            object.entryCount = entry[1];
            object.entriesAddress = word(entry + 4);
            result.push_back(object);
        }
    };
    // The recovered LoadClsnAndObjects always visits the global table first
    // then the area tables in increasing index order.
    require(descriptor.objectsAddress != 0, "Missing original global object table.");
    readTable(descriptor.objectsAddress, -1);
    require(descriptor.subTableCount <= kAreaLimit, "Invalid original stage area count.");
    if (descriptor.subTableCount) {
        require(descriptor.subTablesAddress != 0, "Missing original stage area table.");
        const uint8_t *areas = at(descriptor.subTablesAddress,
                                  size_t(descriptor.subTableCount) * 12);
        for (unsigned i = 0; i < descriptor.subTableCount; ++i) {
            const uint32_t tableAddress = word(areas + size_t(i) * 12);
            if (tableAddress)
                readTable(tableAddress, static_cast<int>(i));
        }
    }
    return result;
}

const uint8_t *StageAssets::overlay_at(uint32_t guest, size_t bytes) const {
    if (guest < overlay_.base ||
        static_cast<uint64_t>(guest) - overlay_.base > overlay_.bytes.size() ||
        bytes > overlay_.bytes.size() - (static_cast<uint64_t>(guest) - overlay_.base))
        throw std::runtime_error("Native stage reference is outside its original loaded overlay.");
    return overlay_.bytes.data() + (guest - overlay_.base);
}

StageAssets StageAssets::load(Cartridge &cartridge, uint32_t sublevel) {
    require(sublevel < 0x34, "Invalid native sublevel number.");
    // dScStage_c::LoadLevelOverlays reads the *word* map at 020758c8,
    // whereas InitResources reads the independent LVL_Overlay word table
    // at 02092208. These are not sequentially derived asset IDs.
    const auto id = cartridge.arm9(0x020758c8u + sublevel * 4, 4);
    const auto address = cartridge.arm9(0x02092208u + sublevel * 4, 4);
    const uint32_t overlayId = word(id.data());
    const uint32_t levelAddress = word(address.data());
    require(overlayId < 512 && levelAddress, "Unmapped native level overlay.");
    StageAssets assets;
    assets.descriptor_.level = sublevel;
    assets.descriptor_.overlayId = overlayId;
    assets.descriptor_.levelAddress = levelAddress;
    assets.overlay_ = cartridge.overlay_data(overlayId);
    // LVL_Overlay is a ROM-defined record; C++ class sizes on AArch64 are
    // irrelevant. Read its documented byte offsets without pointer casts.
    const uint8_t *record = assets.overlay_at(levelAddress, 0x1c);
    assets.descriptor_.clpsAddress = word(record);
    assets.descriptor_.objectsAddress = word(record + 4);
    assets.descriptor_.modelHandle = half(record + 8);
    assets.descriptor_.collisionHandle = half(record + 0xa);
    assets.descriptor_.minimapCharacters = half(record + 0xc);
    assets.descriptor_.minimapPalette = half(record + 0xe);
    assets.descriptor_.subTablesAddress = word(record + 0x10);
    assets.descriptor_.subTableCount = record[0x14];
    assets.descriptor_.flags = record[0x15];
    assets.objects_ = decode_object_catalog(assets.overlay_, assets.descriptor_);
    // Overlay 2 contains the category dispatch and actor-ID tables used by
    // the original stage loader, independent from the level's own overlay.
    assets.objectOverlay_ = cartridge.overlay_data(2);
    for (auto &category : assets.objects_) {
        category.loaderAddress = original_loader(assets.objectOverlay_, category.type);
        if (category.loaderAddress == 0x020fe960u) {
            auto spawns = decode_simple_spawns(assets.overlay_, assets.objectOverlay_, category);
            assets.simpleSpawns_.insert(assets.simpleSpawns_.end(),
                                        spawns.begin(), spawns.end());
        }
    }
    require(assets.descriptor_.modelHandle && assets.descriptor_.collisionHandle,
            "The native level has no original model or collision resource.");
    // Actual cartridge handles, not hard-coded castle replacements.
    assets.model_ = cartridge.file(assets.descriptor_.modelHandle);
    assets.collision_ = cartridge.file(assets.descriptor_.collisionHandle);
    require(assets.model_.size() >= 0x20 && assets.collision_.size() >= 0x20,
            "Original native stage model or collision file is truncated.");
    return assets;
}
}

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
}

namespace sm64ds {

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

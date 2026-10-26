#include "stage_assets.h"
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

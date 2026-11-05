#include "minigame_catalog.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace {
uint16_t read16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int32_t signed32(const uint8_t *p) {
    const uint32_t bits = read32(p);
    int32_t result = 0;
    static_assert(sizeof(result) == sizeof(bits), "32-bit cartridge fields");
    std::memcpy(&result, &bits, sizeof result);
    return result;
}
// The byte-level scan is deliberately constrained by facts from the recovered
// d_s_mini_gm.cpp: 36 rows, IDs 0x169..0x186, 30 unique games plus six second
// variants, and a unique 0..35 row tag in sceneParameter bits 8..23.
// This is data discovery, not execution of ARM9 code.
bool decode_table(const uint8_t *p,
                  std::array<sm64ds::MinigameEntry, sm64ds::MinigameCatalog::Count> &rows) {
    std::array<uint8_t, 36> tags{};
    std::array<uint8_t, 30> games{};
    for (size_t i = 0; i < rows.size(); ++i) {
        const uint8_t *at = p + i * 0x34;
        sm64ds::MinigameEntry item{};
        item.sceneId = read16(at);
        item.padding = read16(at + 2);
        item.sceneParameter = read32(at + 4);
        item.scoringMode = signed32(at + 8);
        item.scoreCap = signed32(at + 12);
        item.ownRecords = signed32(at + 16);
        for (unsigned j = 0; j < 5; ++j)
            item.recordDefaults[j] = signed32(at + 20 + j * 4);
        item.musicTrack = signed32(at + 40);
        item.musicMode = signed32(at + 44);
        item.level = signed32(at + 48);
        if (!sm64ds::MinigameCatalog::is_game_scene(item.sceneId) ||
            item.scoringMode < 0 || item.scoringMode > 5 ||
            item.ownRecords < 0 || item.ownRecords > 1)
            return false;
        const uint32_t tag = (item.sceneParameter >> 8) & 0xffff;
        if (tag >= tags.size() || tags[tag])
            return false;
        ++tags[tag];
        auto &count = games[item.sceneId - sm64ds::MinigameCatalog::FirstGameScene];
        if (++count > 2) return false;
        rows[i] = item;
    }
    unsigned duplicates = 0;
    for (const auto count : games) {
        if (!count) return false;
        duplicates += (count == 2);
    }
    return duplicates == 6;
}
}

namespace sm64ds {
unsigned MinigameCatalog::overlay_for_scene(uint32_t scene) {
    if (scene == 5) return 5;
    if (is_game_scene(scene)) return 6;
    throw std::out_of_range("Requested scene has no original minigame overlay.");
}

MinigameCatalog MinigameCatalog::parse(const uint8_t *bytes, size_t size,
                                        uint32_t imageBase) {
    constexpr size_t TableSize = Count * sizeof(MinigameEntry);
    if (!bytes || size < TableSize)
        throw std::runtime_error("Missing original minigame table in overlay 5.");
    MinigameCatalog found{};
    bool matched = false;
    for (size_t offset = 0; offset <= size - TableSize; offset += 4) {
        if (!is_game_scene(read16(bytes + offset))) continue;
        std::array<MinigameEntry, Count> rows{};
        if (!decode_table(bytes + offset, rows)) continue;
        if (matched)
            throw std::runtime_error("Ambiguous original minigame table in overlay 5.");
        if (uint64_t(imageBase) + offset > UINT32_MAX)
            throw std::runtime_error("Minigame table guest address overflow.");
        found.menuTableAddress_ = static_cast<uint32_t>(imageBase + offset);
        found.rows_ = rows;
        matched = true;
    }
    if (!matched)
        throw std::runtime_error("Could not validate the original 36-row minigame table.");
    return found;
}

MinigameCatalog MinigameCatalog::load(Cartridge &cartridge) {
    const auto menu = cartridge.overlay_data(overlay_for_scene(5));
    // Verify that original game code/data is present, but never call ARM9
    // functions from the data image as host function pointers.
    const auto games = cartridge.overlay_data(overlay_for_scene(FirstGameScene));
    if (games.bytes.empty())
        throw std::runtime_error("Original minigame gameplay overlay 6 is missing.");
    return parse(menu.bytes.data(), menu.bytes.size(), menu.base);
}

const MinigameEntry &MinigameCatalog::row(size_t index) const {
    return rows_.at(index);
}
}

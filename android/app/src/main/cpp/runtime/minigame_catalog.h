#pragma once
#include "cartridge.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace sm64ds {

// The original overlay-5 table has 36 0x34-byte entries. Keep its cartridge
// fields at their original widths; no native pointers or fabricated records.
struct MinigameEntry {
    uint16_t sceneId;
    uint16_t padding;
    uint32_t sceneParameter;
    int32_t scoringMode;
    int32_t scoreCap;
    int32_t ownRecords;
    std::array<int32_t, 5> recordDefaults;
    int32_t musicTrack;
    int32_t musicMode;
    int32_t level;
};
static_assert(sizeof(MinigameEntry) == 0x34, "Original MgEntry ABI");

class MinigameCatalog {
public:
    static constexpr size_t Count = 36;
    static constexpr uint32_t FirstGameScene = 0x169;
    static constexpr uint32_t LastGameScene = 0x186;
    static bool is_game_scene(uint32_t id) noexcept {
        return id >= FirstGameScene && id <= LastGameScene;
    }
    // Original GetSceneOverlayID: menu 5 -> overlay 5, game scenes -> overlay 6.
    static unsigned overlay_for_scene(uint32_t id);
    // Parse the original immutable table from a cartridge overlay image.
    // An absent or ambiguous table is an error, never a synthetic menu.
    static MinigameCatalog parse(const uint8_t *bytes, size_t size, uint32_t imageBase = 0);
    static MinigameCatalog load(Cartridge &cartridge);
    const MinigameEntry &row(size_t index) const;
    uint32_t menu_table_address() const noexcept { return menuTableAddress_; }

private:
    std::array<MinigameEntry, Count> rows_{};
    uint32_t menuTableAddress_ = 0;
};
}

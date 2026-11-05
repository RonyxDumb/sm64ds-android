#pragma once
#include <cstddef>
#include <cstdint>

namespace sm64ds {
struct MinigameEntry;
// Native access to the same 0x2e4-byte live minigame block that the recovered
// SaveData::ReadMinigameData/SaveMinigames use. No substitute save format.
class MinigameProgress {
public:
    static constexpr size_t GameRows = 36;
    static constexpr size_t RecordRanks = 5;
    static bool unlocked(size_t row);
    static bool is_new(size_t row);
    static void unlock(size_t row);
    static void clear_new(size_t row);
    // 'slot' is the 0..35 save-table number from original
    // MiniGm_GetSaveTable (the menu row's sceneParameter bits 8..15).
    static int32_t record(size_t slot, size_t rank);
    static void set_record(size_t slot, size_t rank, int32_t value);
    // Exact dScMiniGm_c::InitResources record-seeding and score-cap rules.
    // Call only on entry to the original menu. Returns whether the original
    // live save changed; the scene must persist after the fade permits it.
    static bool initialize_records(const MinigameEntry *rows, size_t count);
    // Persist through the recovered cartridge backup + checksum + mirror path.
    // Returns false when that actual write fails, never a fabricated success.
    static bool persist();
};
}

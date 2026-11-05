#include "minigame_progress.h"
#include "minigame_catalog.h"
#include "save_store.h"
#include "SaveData.h"
#include <cstring>
#include <stdexcept>

extern "C" {
extern int data_0209caa0[];
void func_02013568(int slot, int rank, int value);
int func_02013580(int slot, int rank);
int func_020135d0(int index);
void func_0201361c(int index);
int func_02013638(int index);
}
namespace {
uint8_t *active() {
    (void)sm64ds::SaveStore::active();
    return reinterpret_cast<uint8_t *>(data_0209caa0);
}
uint32_t read_word(const uint8_t *p) {
    uint32_t word;
    std::memcpy(&word, p, sizeof word);
    return word;
}
void write_word(uint8_t *p, uint32_t word) {
    std::memcpy(p, &word, sizeof word);
}
void check_row(size_t row) {
    if (row >= sm64ds::MinigameProgress::GameRows)
        throw std::out_of_range("Invalid original minigame menu row.");
}
void check_record(size_t slot, size_t rank) {
    if (slot >= sm64ds::MinigameProgress::GameRows ||
        rank >= sm64ds::MinigameProgress::RecordRanks)
        throw std::out_of_range("Invalid original minigame save table index.");
}
}

namespace sm64ds {
bool MinigameProgress::unlocked(size_t row) {
    check_row(row);
    // Original MiniGm_IsUnlocked: the first eight entries are always open;
    // func_02013638 checks bits 0..27 of the word at 0x48 otherwise.
    if (row < 8) return true;
    (void)active();
    return func_02013638(static_cast<int>(row - 8)) != 0;
}
bool MinigameProgress::is_new(size_t row) {
    check_row(row);
    (void)active();
    // Preserve the original helper's index remapping, rather than a
    // second native copy of its record bitfield computation.
    return func_020135d0(static_cast<int>(row < 8 ? row + 28 : row - 8)) != 0;
}
void MinigameProgress::unlock(size_t row) {
    check_row(row);
    if (row < 8) return;
    (void)active();
    func_0201361c(static_cast<int>(row - 8));
}
void MinigameProgress::clear_new(size_t row) {
    check_row(row);
    auto *save = active();
    if (row < 8) {
        save[0x50] |= uint8_t(1u << row);
    } else {
        auto *p = save + 0x4c;
        write_word(p, read_word(p) | (uint32_t(1) << (row - 8)));
    }
}
int32_t MinigameProgress::record(size_t slot, size_t rank) {
    check_record(slot, rank);
    (void)active();
    return func_02013580(static_cast<int>(slot), static_cast<int>(rank));
}
void MinigameProgress::set_record(size_t slot, size_t rank, int32_t value) {
    check_record(slot, rank);
    (void)active();
    func_02013568(static_cast<int>(slot), static_cast<int>(rank), value);
}
bool MinigameProgress::initialize_records(const MinigameEntry *rows, size_t count) {
    if (!rows || count != GameRows)
        throw std::invalid_argument("Original minigame menu requires exactly 36 records.");
    auto *save = active();
    // ROM source: dScMiniGm_c::InitResources seeds all five records for each
    // game exactly once, guarded by bit 31 of the unlocked/initialized word.
    const bool initial = (read_word(save + 0x48) & 0x80000000u) == 0;
    bool dirty = initial;
    if (initial) {
        for (size_t i = 0; i < GameRows; ++i)
            for (size_t rank = 0; rank < RecordRanks; ++rank)
                set_record(i, rank, rows[i].recordDefaults[rank]);
        write_word(save + 0x48, read_word(save + 0x48) | 0x80000000u);
    }
    // Preserve recovered unsigned comparison: negative/sentinel records
    // greater than a nonzero score cap are reset to the ROM defaults.
    for (size_t i = 0; i < GameRows; ++i) {
        const auto &row = rows[i];
        if (row.scoreCap == 0) continue;
        const size_t compareCount = row.ownRecords == 1 ? 1 : RecordRanks;
        bool exceedsCap = false;
        for (size_t rank = 0; rank < compareCount; ++rank) {
            if (static_cast<uint32_t>(record(i, rank)) >
                static_cast<uint32_t>(row.scoreCap)) {
                exceedsCap = true;
                break;
            }
        }
        if (!exceedsCap) continue;
        for (size_t rank = 0; rank < RecordRanks; ++rank)
            set_record(i, rank, row.recordDefaults[rank]);
        dirty = true;
    }
    return dirty;
}
bool MinigameProgress::persist() {
    auto *save = active();
    return SaveData::SaveMinigames(reinterpret_cast<MinigameSaveData *>(save + 0x44)) == 1;
}
}

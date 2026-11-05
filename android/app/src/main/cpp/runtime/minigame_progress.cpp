#include "minigame_progress.h"
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
bool MinigameProgress::persist() {
    auto *save = active();
    return SaveData::SaveMinigames(reinterpret_cast<MinigameSaveData *>(save + 0x44)) == 1;
}
}

#include "minigame_progress.h"
#include "save_store.h"
#include "SaveData.h"
#include <cstring>
#include <stdexcept>

extern "C" int data_0209caa0[];
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
size_t record_offset(size_t slot, size_t rank) {
    if (slot >= sm64ds::MinigameProgress::GameRows ||
        rank >= sm64ds::MinigameProgress::RecordRanks)
        throw std::out_of_range("Invalid original minigame save table index.");
    // Original func_02013568/580: data_0209caf4[slot][rank].
    // data_0209caf4 == data_0209caa0 + 0x54.
    return 0x54 + slot * 5 * sizeof(uint32_t) + rank * sizeof(uint32_t);
}
}

namespace sm64ds {
bool MinigameProgress::unlocked(size_t row) {
    check_row(row);
    // Original MiniGm_IsUnlocked: the first eight entries are always open;
    // func_02013638 checks bits 0..27 of the word at 0x48 otherwise.
    if (row < 8) return true;
    return (read_word(active() + 0x48) & (uint32_t(1) << (row - 8))) != 0;
}
bool MinigameProgress::is_new(size_t row) {
    check_row(row);
    const auto *save = active();
    // Original MiniGm_IsNew/func_020135d0 invert the "seen" bits.
    if (row < 8) return (save[0x50] & (1u << row)) == 0;
    return (read_word(save + 0x4c) & (uint32_t(1) << (row - 8))) == 0;
}
void MinigameProgress::unlock(size_t row) {
    check_row(row);
    if (row < 8) return;
    auto *p = active() + 0x48;
    write_word(p, read_word(p) | (uint32_t(1) << (row - 8)));
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
    const uint32_t bits = read_word(active() + record_offset(slot, rank));
    int32_t value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
void MinigameProgress::set_record(size_t slot, size_t rank, int32_t value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    write_word(active() + record_offset(slot, rank), bits);
}
bool MinigameProgress::persist() {
    auto *save = active();
    return SaveData::SaveMinigames(reinterpret_cast<MinigameSaveData *>(save + 0x44)) == 1;
}
}

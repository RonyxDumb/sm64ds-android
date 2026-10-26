#pragma once
#include "runtime/title_records.h"
#include "title_config.h"
#include <array>
#include <cstddef>

namespace sm64ds {
struct FileSummary {
    bool used;
    u8 characters, secretStars;
    std::array<u8, 15> stars, coins, starZero;
};
}

extern "C" {
extern FileSaveData *data_0209b33c;
extern sm64ds::TitleConfig nativeTitleConfig;
extern u32 data_0209b340[0x9c / 4];
extern u8 data_0209b34b[], data_0209b34e[], data_0209b3d8[], data_0208ee3c;
extern u8 data_0209d4b4;
int func_02013c84(u8 slot, FileSaveData *source, s32 fileIndex, FileSaveData *target);
void func_ov007_020cc0e4(FileSaveData *files);
FileSaveData *func_ov007_020cc0cc(int slot);
void func_ov007_020cc168(u32 slot);
int GetSoundMode();
int IsStarCollected(s32 course, s32 star);
u8 CountStarsCollectedInLevelToDisplay(s32 course);
}

namespace sm64ds {
// Native ownership of the original three file records. The recovered title
// launch branch consumes this same buffer; the UI state machine is not replaced.
class TitleFiles {
public:
    explicit TitleFiles(const u8 *messageBank);
    ~TitleFiles();
    TitleFiles(const TitleFiles &) = delete;
    TitleFiles &operator=(const TitleFiles &) = delete;
    const FileSaveData &file(unsigned slot) const;
    bool read_ok(unsigned slot) const;
    FileSummary summary(unsigned slot) const;
    const TitleConfig &config() const;
    bool start_selected(int result);
    u8 command(s32 value);

private:
    bool load();
    std::array<FileSaveData, 3> files_{};
    std::array<bool, 3> readOk_{};
};
}

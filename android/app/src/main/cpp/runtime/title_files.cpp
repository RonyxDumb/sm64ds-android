#include "title_files.h"
#include "SaveData.h"
#include <algorithm>
#include <stdexcept>

extern "C" {
FileSaveData *data_0209b33c = nullptr;
sm64ds::TitleConfig nativeTitleConfig{};
u8 data_0209d4b4 = 2, data_0208ee3c = 1;
asm(".global data_0209b340\n.set data_0209b340, nativeTitleConfig\n"
    ".global data_0209b34b\n.set data_0209b34b, nativeTitleConfig + 0xb\n"
    ".global data_0209b34e\n.set data_0209b34e, nativeTitleConfig + 0xe\n"
    ".global data_0209b3d8\n.set data_0209b3d8, nativeTitleConfig + 0x98\n");
}

namespace sm64ds {
TitleFiles::TitleFiles(const u8 *messageBank) {
    if (data_0209b33c)
        throw std::runtime_error("A title file buffer is already active.");
    nativeTitleConfig = {};
    nativeTitleConfig.messageBank = messageBank;
    try {
        load(); // command 20's recovered read/activate/summary loop
    } catch (...) {
        if (data_0209b33c == files_.data())
            data_0209b33c = nullptr;
        nativeTitleConfig.messageBank = nullptr;
        throw;
    }
}
TitleFiles::~TitleFiles() {
    if (data_0209b33c == files_.data()) {
        data_0209b33c = nullptr;
        nativeTitleConfig.messageBank = nullptr;
    }
}
const FileSaveData &TitleFiles::file(unsigned slot) const {
    return files_.at(slot);
}
bool TitleFiles::read_ok(unsigned slot) const {
    return readOk_.at(slot);
}
const TitleConfig &TitleFiles::config() const {
    return nativeTitleConfig;
}
FileSummary TitleFiles::summary(unsigned slot) const {
    if (slot >= files_.size())
        throw std::out_of_range("Invalid title file slot.");
    FileSummary result{};
    result.used = data_0209b34b[slot] != 0;
    result.characters = data_0209b34e[slot];
    result.secretStars = data_0209b3d8[slot];
    const auto *bytes = nativeTitleConfig.bytes.data() + slot * 15;
    std::copy_n(bytes + 0x11, 15, result.stars.begin());
    std::copy_n(bytes + 0x3e, 15, result.coins.begin());
    std::copy_n(bytes + 0x6b, 15, result.starZero.begin());
    return result;
}
}

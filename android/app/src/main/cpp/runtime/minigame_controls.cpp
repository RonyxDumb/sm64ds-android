#include "minigame_controls.h"
#include "minigame_progress.h"
#include <stdexcept>

namespace sm64ds {
bool MinigameMenuControls::has_second_page() {
    // Original MiniGm_HasSecondPage scans 24..35 across the four groups.
    for (size_t row = 24; row < MinigameProgress::GameRows; ++row)
        if (MinigameProgress::unlocked(row)) return true;
    return false;
}
MinigameTap MinigameMenuControls::tap(int x, int y, unsigned page, unsigned group,
                                     bool inputAllowed) {
    if (page > 1 || group > 3)
        throw std::out_of_range("Invalid original minigame page or group.");
    if (!inputAllowed || x < 0 || x >= 256 || y < 0 || y >= 192)
        return {};
    // Original group tabs: <= 0x30 x, 4 rows with a 0x30-pixel pitch.
    if (x <= 0x30 && page <= 1) {
        for (unsigned i = 0; i < 4; ++i) {
            const int top = 2 + static_cast<int>(i) * 0x30;
            if (y >= top && y <= top + 0x2c && i != group)
                return {MinigameTapKind::Group, static_cast<int>(i)};
        }
    }
    // Arrow and exit button: the original handlers are gated by their
    // page/animation timers. Here the caller supplies that gate.
    if (has_second_page() && y >= 0x40 && y <= 0x80) {
        if (page == 0 && x >= 0xd8 && x <= 0xf8)
            return {MinigameTapKind::NextPage, -1};
        if (page == 1 && x >= 0x38 && x <= 0x58)
            return {MinigameTapKind::PreviousPage, -1};
    }
    if (x >= 0xd8 && x <= 0xf8 && y >= 0x90 && y <= 0xb0)
        return {MinigameTapKind::Exit, -1};
    int chosen = -1;
    if (page == 0) {
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 3; ++col) {
                const int left = 0x40 + col * 0x30;
                const int top = 0x18 + row * 0x48;
                if (x >= left && x < left + 0x30 &&
                    y >= top && y < top + 0x48)
                    chosen = static_cast<int>(group) + row * 0xc + col * 4;
            }
        }
    } else {
        for (int col = 0; col < 3; ++col) {
            const int left = 0x60 + col * 0x30;
            if (x >= left && x < left + 0x30 && y >= 0x38 && y < 0x80)
                chosen = static_cast<int>(group) + 0x18 + col * 4;
        }
    }
    if (chosen >= 0 && MinigameProgress::unlocked(static_cast<size_t>(chosen)))
        return {MinigameTapKind::Select, chosen};
    return {};
}
}

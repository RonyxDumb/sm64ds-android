#pragma once
#include <cstdint>

namespace sm64ds {
// The recovered MiniGm_UpdateGroupTabs, MiniGm_UpdatePageArrows,
// dScMiniGm_c::UpdateGameSelect and MiniGm_UpdateExitButton hitboxes.
// This class only resolves stylus taps; scene state, original render,
// animations, fader timers and game launches are NOT provided here.
enum class MinigameTapKind : uint8_t {
    None, Group, PreviousPage, NextPage, Select, Exit
};
struct MinigameTap {
    MinigameTapKind kind = MinigameTapKind::None;
    int index = -1; // selected 0..35 menu row or selected 0..3 group
};
class MinigameMenuControls {
public:
    static bool has_second_page();
    // Called on a stylus down-edge only, and only when the original scene's
    // fader, scroll and exit timers permit input. Coordinates are DS 256x192.
    static MinigameTap tap(int x, int y, unsigned page, unsigned group,
                           bool inputAllowed = true);
};
}

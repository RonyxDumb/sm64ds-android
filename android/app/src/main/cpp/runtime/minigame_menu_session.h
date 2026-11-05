#pragma once
#include "minigame_controls.h"
#include <cstdint>

namespace sm64ds {
// Source-faithful non-graphics part of dScMiniGm_c::Behavior. The view and
// playback are supplied by the original renderer/overlay, not this class.
// A selection event is a request to the scene dispatcher, not gameplay.
enum class MinigameMenuEvent : uint8_t { None, Selection, Exit };
struct MinigameMenuResult {
    MinigameMenuEvent event = MinigameMenuEvent::None;
    int row = -1;
};
class MinigameMenuSession {
public:
    explicit MinigameMenuSession(unsigned page = 0, unsigned group = 0);
    MinigameMenuResult tick(bool touchEdge, int x, int y, bool faderReady);
    unsigned page() const noexcept { return page_; }
    unsigned group() const noexcept { return group_; }
    int scroll_x() const noexcept { return scrollX_; }
    unsigned blink_phase() const noexcept { return blinkPhase_; }
    unsigned arrow_phase() const noexcept { return arrowPhase_; }
    unsigned previous_page_timer() const noexcept { return previousTimer_; }
    unsigned next_page_timer() const noexcept { return nextTimer_; }
    unsigned exit_timer() const noexcept { return exitTimer_; }
    bool scrolling() const noexcept { return scrolling_; }
    bool exiting() const noexcept { return exiting_; }

private:
    unsigned page_ = 0, group_ = 0;
    int scrollX_ = 0;
    unsigned previousTimer_ = 0, nextTimer_ = 0, exitTimer_ = 0;
    unsigned blinkPhase_ = 0, arrowPhase_ = 0, groupDelay_ = 0;
    bool scrolling_ = false, exiting_ = false;
};
}

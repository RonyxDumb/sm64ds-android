#include "minigame_menu_session.h"
#include <stdexcept>

namespace sm64ds {
MinigameMenuSession::MinigameMenuSession(unsigned page, unsigned group)
    : page_(page), group_(group), scrollX_(page ? 0xb0 : 0) {
    if (page > 1 || group > 3)
        throw std::out_of_range("Invalid recovered minigame page or character tab.");
}
MinigameMenuResult MinigameMenuSession::tick(bool touchEdge, int x, int y,
                                              bool faderReady) {
    if (exiting_) return {};
    // dScMiniGm_c::Behavior: page timer 0x12, exit timer 0x1c and
    // phase counters 0..0x3f. The exit fade is requested at timer == 1.
    if (previousTimer_ > 0) {
        if (--previousTimer_ == 0) {
            page_ = 0;
            scrolling_ = true;
        }
    } else if (nextTimer_ > 0) {
        if (--nextTimer_ == 0) {
            page_ = 1;
            scrolling_ = true;
        }
    } else if (exitTimer_ > 1) {
        if (--exitTimer_ == 1) {
            exiting_ = true;
            return {MinigameMenuEvent::Exit, -1};
        }
    } else {
        arrowPhase_ = (arrowPhase_ + 1) & 0x3f;
        blinkPhase_ = (blinkPhase_ + 1) & 0x3f;
    }
    if (groupDelay_ > 0) --groupDelay_;
    // The original handler gates input on fade-ready, page animation,
    // outstanding timers and the scene's pending destroy state. A stylus
    // down-edge is required; held/repeated taps do not trigger transitions.
    const bool inputReady = faderReady && !scrolling_ &&
        previousTimer_ == 0 && nextTimer_ == 0 && exitTimer_ == 0;
    if (touchEdge && inputReady) {
        const auto hit = MinigameMenuControls::tap(x, y, page_, group_);
        switch (hit.kind) {
        case MinigameTapKind::Group:
            group_ = static_cast<unsigned>(hit.index);
            groupDelay_ = 0x1e; // original icon-change debounce
            break;
        case MinigameTapKind::PreviousPage:
            previousTimer_ = 0x12;
            break;
        case MinigameTapKind::NextPage:
            nextTimer_ = 0x12;
            break;
        case MinigameTapKind::Select:
            exiting_ = true;
            return {MinigameMenuEvent::Selection, hit.index};
        case MinigameTapKind::Exit:
            exitTimer_ = 0x1c;
            break;
        case MinigameTapKind::None:
            break;
        }
    }
    // MiniGm_UpdatePageScroll advances eight pixels per native DS tick,
    // and releases mPageFlipped precisely when it reaches 0 or 0xb0.
    if (faderReady && previousTimer_ == 0 && nextTimer_ == 0 &&
        exitTimer_ == 0 && scrolling_) {
        const int target = page_ ? 0xb0 : 0;
        if (scrollX_ < target)
            scrollX_ = scrollX_ + 8 > target ? target : scrollX_ + 8;
        else if (scrollX_ > target)
            scrollX_ = scrollX_ - 8 < target ? target : scrollX_ - 8;
        if (scrollX_ == target) scrolling_ = false;
    }
    return {};
}
}

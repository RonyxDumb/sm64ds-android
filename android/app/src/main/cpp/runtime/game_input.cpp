#include "game_input.h"
#include "PlayerInput.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
struct RawTouch {
    uint16_t x, y, held, validity;
} touch[4]{};
uint16_t keys[4]{};
float axis(float value) {
    return std::isfinite(value) ? std::clamp(value, -1.f, 1.f) : 0.f;
}
}
extern "C" {
uint8_t gActivePlayerSlot = 0;
uint8_t gTouchHeld[16]{};
uint8_t data_020a0e44 = 0;
uint16_t data_020a0e48[4]{}, data_020a0e50[4]{};
// One actual four-byte key pair per player, as in the recovered input TU.
uint16_t data_020a0e58[8]{};
// ELF aliases preserve the original interior byte lanes, instead of copying
// them into separate globals that would drift out of sync on ARM64.
asm(".global gTouchEdge\n.set gTouchEdge, gTouchHeld + 1\n"
    ".global gTouchX\n.set gTouchX, gTouchHeld + 2\n"
    ".global gTouchY\n.set gTouchY, gTouchHeld + 3\n"
    ".global data_020a0e5a\n.set data_020a0e5a, data_020a0e58 + 2\n");
RawTouch *func_0203dabc(int slot) {
    if (slot < 0 || slot >= 4)
        throw std::out_of_range("Invalid input player slot.");
    return &touch[slot];
}
uint16_t func_0203dae4(int slot) {
    if (slot < 0 || slot >= 4)
        throw std::out_of_range("Invalid input player slot.");
    return keys[slot];
}
int func_0203daac() {
    return 1;
} // this native runtime owns one local player
void func_0203bb60();
void func_0203bc7c();
}

namespace sm64ds {
GameInput::GameInput() {
    clear();
}
GameInput::~GameInput() {
    clear();
}
void GameInput::clear() {
    std::memset(touch, 0, sizeof touch);
    std::memset(keys, 0, sizeof keys);
    std::memset(gTouchHeld, 0, sizeof gTouchHeld);
    std::memset(data_020a0e48, 0, sizeof data_020a0e48);
    std::memset(data_020a0e50, 0, sizeof data_020a0e50);
    std::memset(data_020a0e58, 0, sizeof data_020a0e58);
    data_020a0e44 = gActivePlayerSlot = 0;
    x_ = y_ = 0;
}
void GameInput::update(uint32_t buttons, float x, float y, int tx, int ty, bool stickAsButtons) {
    x_ = axis(x);
    y_ = axis(y);
    keys[0] = static_cast<uint16_t>(buttons & 0xfff);
    if (stickAsButtons) {
        if (x_ > .35f)
            keys[0] |= 0x10;
        if (x_ < -.35f)
            keys[0] |= 0x20;
        if (y_ > .35f)
            keys[0] |= 0x40;
        if (y_ < -.35f)
            keys[0] |= 0x80;
    }
    const bool down = tx >= 0 && tx < 256 && ty >= 0 && ty < 192;
    touch[0] = {static_cast<uint16_t>(down ? tx : 0), static_cast<uint16_t>(down ? ty : 0),
                static_cast<uint16_t>(down), 0};
    // The original code performs opposite-direction filtering, press/release
    // edges, reset-chord detection and the four-byte stylus state conversion.
    func_0203bc7c();
    func_0203bb60();
}
uint16_t GameInput::held(unsigned slot) const {
    if (slot >= 4)
        throw std::out_of_range("Invalid input player slot.");
    return data_020a0e58[slot * 2];
}
uint16_t GameInput::raw_buttons() const { return keys[0]; }
uint16_t GameInput::pressed(unsigned slot) const {
    if (slot >= 4)
        throw std::out_of_range("Invalid input player slot.");
    return data_020a0e58[slot * 2 + 1];
}
uint16_t GameInput::released(unsigned slot) const {
    if (slot >= 4)
        throw std::out_of_range("Invalid input player slot.");
    return data_020a0e48[slot];
}
}

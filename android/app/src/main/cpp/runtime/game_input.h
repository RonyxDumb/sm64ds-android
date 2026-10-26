#pragma once
#include <cstdint>

namespace sm64ds {
class GameInput {
public:
    GameInput();
    ~GameInput();
    void update(uint32_t buttons, float x, float y, int touchX, int touchY,
                bool stickAsButtons = true);
    void clear();
    uint16_t held(unsigned slot = 0) const;
    uint16_t pressed(unsigned slot = 0) const;
    uint16_t released(unsigned slot = 0) const;
    float x() const {
        return x_;
    }
    float y() const {
        return y_;
    }

private:
    float x_ = 0, y_ = 0;
};
}

#pragma once
#include <cstdint>
namespace sm64ds {
enum class SoundOp { Effect, Stop, Fade, PlayerParam, TrackParam, Status, Music, Mode };
struct SoundRequest {
    SoundOp op;
    int player;
    uint32_t token;
    int a=0, b=0, c=0;
};
}

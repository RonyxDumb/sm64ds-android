#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sm64ds {
// The DS record has 0x9c scalar bytes followed by a four-byte message handle.
// The native owner keeps its pointer at native width; FrontendCore explicitly
// projects that field into its checked address space.
struct TitleConfig {
    std::array<uint8_t, 0x9c> bytes{};
    const uint8_t *messageBank = nullptr;
};
static_assert(offsetof(TitleConfig, messageBank) >= 0x9c);
static_assert(offsetof(TitleConfig, bytes) == 0);
}

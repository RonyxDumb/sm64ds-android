#pragma once
#include "cartridge.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm64ds {
// Original dScMiniGm_c::InitResources cartridge artwork. This only decodes
// source tiles/maps/palettes with their original DS destinations; the OAM
// descriptors, group-icon tables and scene-5 display lifecycle remain separate.
class MinigameMenuArtwork {
public:
    enum class Region : size_t {
        MainBg2Characters, MainBg2Screen, MainBgPalette,
        MainObjCharacters, MainObjPalette, SubBg2Characters,
        SubBgPalette, SubObjCharacters, SubObjPalette, Count
    };
    struct Part {
        uint32_t handle = 0;
        uint32_t destination = 0;
        std::vector<uint8_t> bytes;
    };
    static uint32_t language_obj_file(int ownerLanguage);
    static std::vector<uint8_t> decode_lz(const std::vector<uint8_t> &source,
                                          size_t maxOutput);
    static MinigameMenuArtwork load(Cartridge &cartridge, int ownerLanguage);
    const Part &at(Region region) const {
        return parts_.at(static_cast<size_t>(region));
    }

private:
    std::array<Part, static_cast<size_t>(Region::Count)> parts_{};
};
}

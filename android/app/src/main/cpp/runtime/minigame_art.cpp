#include "minigame_art.h"
#include "ntr/lz77.h"
#include <climits>
#include <stdexcept>
#include <utility>

namespace sm64ds {
uint32_t MinigameMenuArtwork::language_obj_file(int ownerLanguage) {
    if (ownerLanguage < 0 || ownerLanguage > 5)
        throw std::out_of_range("Unsupported original minigame owner language.");
    // dScMiniGm_c::InitResources dispatches exactly these source handles.
    switch (ownerLanguage) {
    case 5: return 0xda;
    case 4: return 0xd7;
    case 3: return 0xd6;
    case 2: return 0xd5;
    default: return 0xd8;
    }
}
std::vector<uint8_t> MinigameMenuArtwork::decode_lz(
    const std::vector<uint8_t> &source, size_t maxOutput) {
    if (source.size() > INT_MAX || maxOutput > INT_MAX)
        throw std::runtime_error("Minigame LZ source or destination exceeds native bounds.");
    const auto expanded = ntr::lz77_size(source.data(), static_cast<int32_t>(source.size()));
    if (expanded <= 0 || static_cast<size_t>(expanded) > maxOutput)
        throw std::runtime_error("Invalid original minigame LZ resource extent.");
    std::vector<uint8_t> result(static_cast<size_t>(expanded));
    if (ntr::lz77_unpack(source.data(), static_cast<int32_t>(source.size()),
                         result.data(), expanded) != expanded)
        throw std::runtime_error("Corrupt original minigame LZ resource.");
    return result;
}
MinigameMenuArtwork MinigameMenuArtwork::load(Cartridge &cartridge, int ownerLanguage) {
    MinigameMenuArtwork art;
    const uint32_t language = language_obj_file(ownerLanguage);
    const auto compressed = [&](Region region, uint32_t file, uint32_t address, size_t maxSize) {
        art.parts_[static_cast<size_t>(region)] = {
            file, address, decode_lz(cartridge.file(file, false), maxSize)};
    };
    const auto palette = [&](Region region, uint32_t file, uint32_t address, size_t size) {
        auto bytes = cartridge.file(file, false);
        if (bytes.size() < size)
            throw std::runtime_error("Truncated original minigame palette resource.");
        bytes.resize(size); // original GX/GXS::Load*Pltt copies precisely size
        art.parts_[static_cast<size_t>(region)] = {file, address, std::move(bytes)};
    };
    // BG2CNT=0x0410 on engine A -> characters 0x06010000, map 0x06002000.
    compressed(Region::MainBg2Characters, 0x4e, 0x06010000, 0x10000);
    compressed(Region::MainBg2Screen,     0x59, 0x06002000, 0x8000);
    palette(Region::MainBgPalette,       0x58, 0x05000000, 0x200);
    compressed(Region::MainObjCharacters, language, 0x06400000, 0x20000);
    palette(Region::MainObjPalette,      0xd9, 0x05000200, 0x100);
    // BG2CNT=0x0404 on engine B -> characters 0x06204000.
    // Its BG2/BG3 maps are loaded separately per character by
    // MiniGm_LoadGroupIcons, so do not substitute a fabricated map.
    compressed(Region::SubBg2Characters, 0x53, 0x06204000, 0x10000);
    palette(Region::SubBgPalette,        0x5a, 0x05000400, 0x200);
    compressed(Region::SubObjCharacters, language, 0x06600000, 0x20000);
    palette(Region::SubObjPalette,       0xd9, 0x05000600, 0x100);
    return art;
}
}

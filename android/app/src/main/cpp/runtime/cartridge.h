#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace sm64ds {
// Cartridge offsets are checked data spans, never native code or objects.
class Cartridge {
public:
    explicit Cartridge(const char *path);
    std::vector<uint8_t> arm9(uint32_t address, uint32_t size) const;
    std::vector<uint8_t> file(uint32_t handle, bool decompress = true);
    static std::vector<uint8_t> narc_member(const std::vector<uint8_t> &, uint32_t index);
private:
    struct Archive { uint16_t first, end; std::string path; };
    uint32_t base_ = 0;
    std::vector<uint8_t> image_;
    std::vector<Archive> archives_;
    std::map<std::string, std::vector<uint8_t>> cached_;
};
}

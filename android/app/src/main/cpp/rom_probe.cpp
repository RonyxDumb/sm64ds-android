#include "rom_probe.h"
#include "ntr/fs.h"
#include "ntr/lz77.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
void verify_game_math();
namespace sm64ds {
namespace {
uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}
struct CloseFile { void operator()(FILE *file) const { fclose(file); } };
struct Unmount { ~Unmount() { ntr::fs_unmount(); } };
}
std::string probe_rom(const char *path) {
    std::unique_ptr<FILE, CloseFile> file(fopen(path, "rb"));
    std::array<uint8_t, 0x160> header{};
    if (!file || fread(header.data(), 1, header.size(), file.get()) != header.size())
        throw std::runtime_error("ROM header is missing or truncated.");
    if (memcmp(header.data() + 12, "ASMP", 4) || header[0x1e] != 0)
        throw std::runtime_error("This build supports European ASMP revision 0 only.");
    const uint16_t expected = header[0x15e] | (header[0x15f] << 8);
    if (crc16(header.data(), 0x15e) != expected)
        throw std::runtime_error("ROM header checksum failed.");
    verify_game_math();
    Unmount cleanup;
    if (!ntr::fs_mount_rom(path))
        throw std::runtime_error("Invalid Nintendo DS filesystem tables.");
    const int32_t id = ntr::fs_id_by_path("data/player/b_mario_all.bmd");
    if (id < 0) throw std::runtime_error("Expected Mario model is missing.");
    const int32_t size = ntr::fs_size(id);
    if (size <= 0 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid model size.");
    std::vector<uint8_t> packed(static_cast<size_t>(size));
    if (ntr::fs_read(id, packed.data(), size) != size)
        throw std::runtime_error("Could not read Mario model.");
    int32_t decoded = size;
    if (size >= 4 && !memcmp(packed.data(), "LZ77", 4)) {
        decoded = ntr::lz77_size(packed.data(), size);
        if (decoded < 0x38 || decoded > 16 * 1024 * 1024)
            throw std::runtime_error("Invalid compressed model size.");
        std::vector<uint8_t> unpacked(static_cast<size_t>(decoded));
        if (ntr::lz77_unpack(packed.data(), size, unpacked.data(), decoded) != decoded)
            throw std::runtime_error("Model decompression failed.");
    }
    return "ASMP revision 0 • " + std::to_string(ntr::fs_file_count()) +
        " files\nNative game math verified.\nNative filesystem mounted; Mario model decoded (" +
        std::to_string(decoded) + " bytes).\n\nGame boot, graphics, controls and audio are not integrated yet.";
}
}

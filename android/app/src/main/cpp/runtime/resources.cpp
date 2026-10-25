#include "resources.h"
#include "SharedFilePtr.h"
#include "ntr/fs.h"
#include "ntr/lz77.h"
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace {
std::atomic<sm64ds::Resources *> current{nullptr};
uint32_t word(const uint8_t *data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}
}

namespace Memory {
void *Allocate(uint32_t size) {
    return sm64ds::Resources::active().heap().allocate(size);
}
void *Allocate(uint32_t size, int alignment) {
    return sm64ds::Resources::active().heap().allocate(size, alignment);
}
void Deallocate(void *pointer) {
    auto &resources = sm64ds::Resources::active();
    resources.heap().deallocate(pointer);
    resources.forget(pointer);
}
}
extern "C" {
uint32_t data_0209d3bc = 0;
uint32_t func_02018a24(uint32_t handle) {
    return sm64ds::Resources::active().file_id(handle);
}
}

// The same card/heap seam as the playable PC port's hal/fs.cpp. LoadFile,
// Construct and Release are recovered source; only card I/O and allocation
// ownership are native. Native SharedFilePtr objects are sizeof(SharedFilePtr),
// never mapped onto an eight-byte cartridge record.
void *SharedFilePtr::Load() {
    data_0209d3bc = fileID;
    if (filePtr) return filePtr;
    const int32_t length = ntr::fs_size(fileID);
    if (length <= 0 || length > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid cartridge resource size.");
    std::vector<uint8_t> packed(static_cast<size_t>(length));
    if (ntr::fs_read(fileID, packed.data(), length) != length)
        throw std::runtime_error("Could not read the cartridge resource.");
    const bool compressed = packed.size() >= 4 && !std::memcmp(packed.data(), "LZ77", 4);
    const int32_t lengthOut = compressed ? ntr::lz77_size(packed.data(), length) : length;
    if (lengthOut <= 0 || lengthOut > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid expanded cartridge resource size.");
    void *allocation = Memory::Allocate(static_cast<uint32_t>(lengthOut), 32);
    if (!allocation) return nullptr;
    if (compressed) {
        if (ntr::lz77_unpack(packed.data(), length, static_cast<uint8_t *>(allocation), lengthOut) != lengthOut) {
            Memory::Deallocate(allocation);
            throw std::runtime_error("Cartridge resource decompression failed.");
        }
    } else {
        std::memcpy(allocation, packed.data(), packed.size());
    }
    try { sm64ds::Resources::active().remember(allocation, static_cast<uint32_t>(lengthOut)); }
    catch (...) { Memory::Deallocate(allocation); throw; }
    filePtr = allocation;
    return filePtr;
}

namespace sm64ds {
std::vector<uint8_t> unpack_overlay(const std::vector<uint8_t> &packed, uint32_t ramSize,
                                    bool compressed) {
    if (!ramSize || ramSize > 4 * 1024 * 1024 || packed.size() > ramSize)
        throw std::runtime_error("Invalid ARM9 overlay size.");
    if (!compressed) {
        if (packed.size() != ramSize)
            throw std::runtime_error("Truncated ARM9 overlay.");
        return packed;
    }
    if (packed.size() < 8)
        throw std::runtime_error("Missing BLZ footer.");
    const size_t end = packed.size();
    const uint32_t descriptor = word(packed.data() + end - 8);
    const size_t header = descriptor >> 24, encoded = descriptor & 0xffffff;
    const uint32_t extra = word(packed.data() + end - 4);
    if (header < 8 || encoded < header || encoded > end ||
        extra != ramSize - end || header > end)
        throw std::runtime_error("Invalid BLZ overlay footer.");
    std::vector<uint8_t> result(ramSize);
    std::memcpy(result.data(), packed.data(), end);
    size_t in = end - header, out = result.size(), limit = end - encoded;
    while (out > limit) {
        if (in <= limit) throw std::runtime_error("Truncated BLZ flags.");
        const uint8_t flags = result[--in];
        for (int bit = 7; bit >= 0 && out > limit; --bit) {
            if (flags & (1u << bit)) {
                if (in - limit < 2) throw std::runtime_error("Truncated BLZ reference.");
                in -= 2;
                const uint16_t code = uint16_t(result[in + 1] << 8) | result[in];
                const size_t count = (code >> 12) + 3, distance = (code & 0xfff) + 3;
                if (count > out - limit || distance > result.size() - out)
                    throw std::runtime_error("Invalid BLZ reference.");
                for (size_t n = 0; n < count; ++n) {
                    --out;
                    result[out] = result[out + distance];
                }
            } else {
                if (in <= limit) throw std::runtime_error("Truncated BLZ literal.");
                result[--out] = result[--in];
            }
            if (out < in) throw std::runtime_error("BLZ input/output overlap.");
        }
    }
    if (in != limit)
        throw std::runtime_error("BLZ overlay did not consume its encoded span.");
    return result;
}
Resources::Resources(const char *path) {
    Resources *expected = nullptr;
    if (!current.compare_exchange_strong(expected, this))
        throw std::runtime_error("A native cartridge session is already active.");
    try {
        if (!ntr::fs_mount_rom(path))
            throw std::runtime_error("Cannot mount the private cartridge.");
        ntr::OverlayInfo overlay{};
        if (!ntr::fs_overlay_info(0, overlay))
            throw std::runtime_error("Cartridge ARM9 overlay 0 is missing.");
        const int32_t size = ntr::fs_size(static_cast<int32_t>(overlay.fileId));
        if (size <= 0 || size > 4 * 1024 * 1024)
            throw std::runtime_error("Invalid overlay 0 file extent.");
        std::vector<uint8_t> packed(static_cast<size_t>(size));
        if (ntr::fs_read(static_cast<int32_t>(overlay.fileId), packed.data(), size) != size)
            throw std::runtime_error("Could not read overlay 0.");
        auto data = unpack_overlay(packed, overlay.ramSize, overlay.compressed);
        // Same version-pinned table and validation as tools/asset_catalog.py.
        // This table is read as data; no cartridge instructions are executed.
        constexpr uint32_t tableAddress = 0x020bd4b8, count = 0x80a;
        if (tableAddress < overlay.ramAddress)
            throw std::runtime_error("Wrong cartridge asset-table address.");
        const size_t table = tableAddress - overlay.ramAddress;
        if (table > data.size() || count * 4 > data.size() - table)
            throw std::runtime_error("Cartridge asset table is outside overlay 0.");
        files_.reserve(count);
        for (uint32_t handle = 0; handle < count; ++handle) {
            const uint32_t address = word(data.data() + table + handle * 4);
            if (address < overlay.ramAddress || address - overlay.ramAddress >= data.size())
                throw std::runtime_error("Invalid cartridge asset path pointer.");
            const size_t offset = address - overlay.ramAddress;
            const char *text = reinterpret_cast<const char *>(data.data() + offset);
            const size_t length = strnlen(text, std::min<size_t>(data.size() - offset, 1025));
            if (!length || length >= 1025 || length == data.size() - offset)
                throw std::runtime_error("Invalid cartridge asset path.");
            const int32_t file = ntr::fs_id_by_path(text);
            if (file < 0 || file > UINT16_MAX)
                throw std::runtime_error("Cartridge asset path has no filesystem entry.");
            files_.push_back(static_cast<uint16_t>(file));
        }
    } catch (...) {
        ntr::fs_unmount();
        current.store(nullptr);
        throw;
    }
}
Resources::~Resources() {
    ntr::fs_unmount();
    current.store(nullptr);
}
Resources &Resources::active() {
    auto *resources = current.load();
    if (!resources) throw std::runtime_error("No native cartridge resource session.");
    return *resources;
}
uint16_t Resources::file_id(uint32_t handle) const {
    if (handle >= files_.size())
        throw std::out_of_range("Unsupported cartridge resource handle.");
    return files_[handle];
}
uint32_t Resources::handle_by_path(const char *path) const {
    const int32_t file = ntr::fs_id_by_path(path);
    for (size_t i = 0; i < files_.size(); ++i)
        if (files_[i] == file) return static_cast<uint32_t>(i);
    throw std::out_of_range("Resource path is absent from the cartridge handle table.");
}
std::vector<uint8_t> Resources::load(const char *path) {
    SharedFilePtr file{};
    file.Construct(handle_by_path(path));
    if (!file.LoadFile()) throw std::bad_alloc();
    struct Release { SharedFilePtr &file; ~Release() { file.Release(); } } release{file};
    const auto size = sizes_.at(file.filePtr);
    const auto *bytes = static_cast<const uint8_t *>(file.filePtr);
    return {bytes, bytes + size};
}
}

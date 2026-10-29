#include "cartridge.h"
#include "resources.h"
#include "ntr/fs.h"
#include "ntr/lz77.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace {
uint16_t half(const uint8_t *p) {
    return uint16_t(p[0]) | uint16_t(p[1] << 8);
}
uint32_t word(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool span(size_t at, size_t size, size_t total) {
    return at <= total && size <= total - at;
}
struct Close {
    void operator()(FILE *p) const {
        std::fclose(p);
    }
};
std::vector<uint8_t> read_file(int32_t id) {
    int32_t size = ntr::fs_size(id);
    require(size > 0 && size <= 16 * 1024 * 1024, "Invalid cartridge file extent.");
    std::vector<uint8_t> result(static_cast<size_t>(size));
    require(ntr::fs_read(id, result.data(), size) == size, "Could not read cartridge file.");
    return result;
}
}
namespace sm64ds {
Cartridge::DataImage Cartridge::arm9_data(uint32_t end) const {
    // src/func_0200497c and Entry use this exact module-parameter record.
    // The expanded file contains ITCM/DTCM payloads *at* the main BSS start.
    // Treating the whole expanded file as initialized RAM leaves instructions
    // in the save, stage and OS globals that CRT0 actually zeroes.
    const auto parameters=arm9(0x02004ad8u,36);
    require(word(parameters.data()+28)==0xdec00621u &&
            word(parameters.data()+32)==0x2106c0deu,"Invalid ARM9 module parameters.");
    const uint32_t table=word(parameters.data()),tableEnd=word(parameters.data()+4);
    const uint32_t payload=word(parameters.data()+8),bss=word(parameters.data()+12),
                   bssEnd=word(parameters.data()+16);
    require(bss>=base_ && uint64_t(bss)-base_<=image_.size() &&
            bssEnd>=bss && end>=bssEnd && end<=0x02400000,
            "Invalid native ARM9 data/BSS boundary.");
    require(tableEnd>=table && (tableEnd-table)%12==0 &&
            table>=base_ && uint64_t(tableEnd)-base_<=image_.size(),
            "Invalid ARM9 autoload table.");
    DataImage result{base_,std::vector<uint8_t>(end-base_,0)};
    std::memcpy(result.bytes.data(),image_.data(),bss-base_);
    uint32_t source=payload;
    for(uint32_t at=table;at<tableEnd;at+=12) {
        const auto row=arm9(at,12);
        const uint32_t destination=word(row.data()),size=word(row.data()+4),zero=word(row.data()+8);
        require(!(destination&3) && !(size&3) && !(zero&3) &&
                uint64_t(destination)+size+zero<=UINT32_MAX,
                "Invalid ARM9 autoload segment extent.");
        const bool itcm=destination>=0x01ff8000u && uint64_t(destination)+size+zero<=0x02000000u;
        const bool dtcm=destination==0x023c0000u && uint64_t(size)+zero<=0x4000;
        require(itcm || dtcm,"Unsupported ARM9 autoload destination.");
        auto content=arm9(source,size);
        require(uint64_t(source)+size<=UINT32_MAX,"ARM9 autoload payload overflow.");
        source+=size;
        content.resize(size_t(size)+zero,0);
        if(dtcm) {
            // Entry clears DTCM after autoload, then installs its IRQ return
            // address at the final word. Native stacks live at host width.
            content.assign(0x4000,0);
            const uint32_t interruptReturn=0x01ffd97cu;
            std::memcpy(content.data()+0x3ffc,&interruptReturn,4);
        }
        result.relocated.push_back({destination,std::move(content)});
    }
    return result;
}
Cartridge::DataImage Cartridge::overlay_data(uint32_t id) const {
    ntr::OverlayInfo info{};
    require(ntr::fs_overlay_info(id, info), "Missing frontend overlay descriptor.");
    require(info.ramSize && uint64_t(info.ramSize) + info.bssSize <= 4 * 1024 * 1024,
            "Invalid frontend overlay data size.");
    auto bytes = unpack_overlay(read_file(info.fileId), info.ramSize, info.compressed);
    bytes.resize(size_t(info.ramSize) + info.bssSize, 0);
    return {info.ramAddress, std::move(bytes)};
}
Cartridge::Cartridge(const char *path) {
    std::unique_ptr<FILE, Close> rom(std::fopen(path, "rb"));
    std::array<uint8_t, 0x160> header{};
    require(rom && std::fread(header.data(), 1, header.size(), rom.get()) == header.size(),
            "Missing boot cartridge header.");
    require(!std::memcmp(header.data() + 12, "ASMP", 4) && header[0x1e] == 0,
            "Native boot requires European ASMP revision 0.");
    base_ = word(header.data() + 0x28);
    uint32_t offset = word(header.data() + 0x20), size = word(header.data() + 0x2c);
    require(base_ == 0x02004000 && size >= 8 && size < 4 * 1024 * 1024,
            "Unexpected boot ARM9 image.");
    image_.resize(size);
    require(std::fseek(rom.get(), offset, SEEK_SET) == 0 &&
                std::fread(image_.data(), 1, size, rom.get()) == size,
            "Truncated boot ARM9 data.");
    uint32_t extra = word(image_.data() + size - 4);
    require(extra <= 4 * 1024 * 1024 - size, "Invalid expanded ARM9 size.");
    image_ = unpack_overlay(image_, size + extra, true);
    auto mounts = arm9(0x0208ecf4, 13 * 20);
    for (unsigned i = 0; i < 13; ++i) {
        const auto *row = mounts.data() + i * 20;
        uint16_t first = half(row + 8), end = half(row + 10);
        uint32_t address = word(row + 16);
        require(first >= 0x8000 && end > first && address >= base_ &&
                    address - base_ < image_.size(),
                "Invalid archive mount row.");
        size_t at = address - base_;
        const char *name = reinterpret_cast<const char *>(image_.data() + at);
        size_t length = strnlen(name, std::min<size_t>(256, image_.size() - at));
        require(length && length < 256 && length < image_.size() - at,
                "Invalid archive mount path.");
        for (const auto &old : archives_)
            require(end <= old.first || first >= old.end, "Overlapping archive IDs.");
        std::string text(name, length);
        if (text.front() == '/')
            text.erase(0, 1);
        require(ntr::fs_id_by_path(text.c_str()) >= 0, "Missing mounted cartridge archive.");
        archives_.push_back({first, end, std::move(text)});
    }
}
std::vector<uint8_t> Cartridge::arm9(uint32_t address, uint32_t size) const {
    require(address >= base_ && span(address - base_, size, image_.size()),
            "Constant outside ARM9 data image.");
    auto begin = image_.begin() + (address - base_);
    return {begin, begin + size};
}
std::vector<uint8_t> Cartridge::narc_member(const std::vector<uint8_t> &image, uint32_t index) {
    require(image.size() >= 16 && !std::memcmp(image.data(), "NARC", 4) &&
                half(image.data() + 4) == 0xfffe && half(image.data() + 12) == 16 &&
                word(image.data() + 8) == image.size(),
            "Invalid NARC header.");
    uint32_t blocks = half(image.data() + 14);
    require(blocks == 3, "Unexpected NARC block count.");
    size_t at = 16, fat = 0, payload = 0, payloadSize = 0;
    uint32_t count = 0;
    for (uint32_t block = 0; block < blocks; ++block) {
        require(span(at, 8, image.size()), "Truncated NARC block.");
        const auto *p = image.data() + at;
        uint32_t size = word(p + 4);
        require(size >= 8 && span(at, size, image.size()), "Invalid NARC block extent.");
        if (!std::memcmp(p, "BTAF", 4)) {
            require(!fat && size >= 12, "Duplicate or short NARC allocation table.");
            count = half(p + 8);
            require(count <= (size - 12) / 8, "Truncated NARC entries.");
            fat = at + 12;
        } else if (!std::memcmp(p, "GMIF", 4)) {
            require(!payload, "Duplicate NARC payload.");
            payload = at + 8;
            payloadSize = size - 8;
        } else
            require(!std::memcmp(p, "BTNF", 4), "Unknown NARC block.");
        at += size;
    }
    require(at == image.size() && fat && payload && index < count, "Missing NARC member.");
    const auto *entry = image.data() + fat + size_t(index) * 8;
    uint32_t begin = word(entry), end = word(entry + 4);
    require(end >= begin && span(begin, end - begin, payloadSize), "NARC member outside payload.");
    return {image.begin() + payload + begin, image.begin() + payload + end};
}
std::vector<uint8_t> Cartridge::file(uint32_t handle, bool decompress) {
    std::vector<uint8_t> data;
    if (handle < 0x8000)
        data = read_file(Resources::active().file_id(handle));
    else {
        for (const auto &archive : archives_) {
            if (handle < archive.first || handle >= archive.end)
                continue;
            auto found = cached_.find(archive.path);
            if (found == cached_.end())
                found =
                    cached_
                        .emplace(archive.path, read_file(ntr::fs_id_by_path(archive.path.c_str())))
                        .first;
            data = narc_member(found->second, handle - archive.first);
            break;
        }
        require(!data.empty(), "Unsupported cartridge archive handle.");
    }
    if (decompress && data.size() >= 8 && !std::memcmp(data.data(), "LZ77", 4)) {
        int32_t size = ntr::lz77_size(data.data(), static_cast<int32_t>(data.size()));
        require(size > 0 && size <= 16 * 1024 * 1024, "Invalid expanded NARC member.");
        std::vector<uint8_t> decoded(static_cast<size_t>(size));
        require(ntr::lz77_unpack(data.data(), static_cast<int32_t>(data.size()), decoded.data(),
                                 size) == size,
                "Invalid compressed NARC member.");
        data = std::move(decoded);
    }
    return data;
}
}

// DS card filesystem: FNT/FAT parsing and a directory backend.
//
// Header layout from GBATEK:
//   0x40 FNT offset, 0x44 FNT size, 0x48 FAT offset, 0x4C FAT size
// FAT is (start, end) u32 pairs, one per file, indexed by file ID.
// FNT is a main table of 8-byte directory records plus name subtables.

#include "ntr/fs.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ntr {
namespace {

struct Entry {
    uint32_t start = 0, end = 0;
    std::string path;
};

struct Fs {
    bool mounted = false;
    bool from_dir = false;
    std::string root;                 // directory backend
    std::vector<uint8_t> rom;         // ROM backend
    std::vector<Entry> files;
    std::unordered_map<std::string, int32_t> by_path;
    int32_t overlays = 0;
};

Fs g;

uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Reject malformed directory graphs before publishing a mount. File names are
// also safe for callers that later materialize the catalog on disk.
bool walk_dir(const uint8_t *fnt, uint32_t fnt_size, uint16_t dir_id,
              uint16_t dirs, const std::string &prefix,
              std::vector<bool> &visited, std::vector<bool> &named, int depth) {
    if (depth > 32 || dir_id < 0xF000 || dir_id - 0xF000 >= dirs) return false;
    const uint32_t index = dir_id - 0xF000;
    if (visited[index]) return false;
    visited[index] = true;
    const uint32_t rec = index * 8u;
    uint32_t off = rd32(fnt + rec);
    uint32_t file_id = rd16(fnt + rec + 4);
    if (off < dirs * 8u || off >= fnt_size) return false;
    while (off < fnt_size) {
        const uint8_t type = fnt[off++];
        if (type == 0) return true;
        const uint32_t len = type & 0x7Fu;
        if (!len || len > fnt_size - off) return false;
        std::string name(reinterpret_cast<const char *>(fnt + off), len);
        off += len;
        if (name == "." || name == ".." ||
            name.find_first_of("/\\:") != std::string::npos ||
            name.find('\0') != std::string::npos || prefix.size() + len > 1024)
            return false;
        if (type & 0x80u) {
            if (fnt_size - off < 2) return false;
            const uint16_t sub = rd16(fnt + off);
            off += 2;
            if (!walk_dir(fnt, fnt_size, sub, dirs, prefix + name + "/",
                          visited, named, depth + 1)) return false;
        } else {
            if (file_id >= g.files.size() || named[file_id]) return false;
            named[file_id] = true;
            g.files[file_id].path = prefix + name;
            if (!g.by_path.emplace(g.files[file_id].path, file_id).second) return false;
            ++file_id;
        }
    }
    return false; // missing subtable terminator
}

}  // namespace

bool fs_mount_rom(const char *rom_path) {
    fs_unmount();

    std::FILE *f = std::fopen(rom_path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0x200 || size > 128 * 1024 * 1024) { std::fclose(f); return false; }

    g.rom.resize(static_cast<size_t>(size));
    const size_t got = std::fread(g.rom.data(), 1, g.rom.size(), f);
    std::fclose(f);
    if (got != g.rom.size()) { g.rom.clear(); return false; }

    const uint32_t fnt_off = rd32(&g.rom[0x40]), fnt_size = rd32(&g.rom[0x44]);
    const uint32_t fat_off = rd32(&g.rom[0x48]), fat_size = rd32(&g.rom[0x4C]);
    if (fat_size == 0 || fat_size % 8 || fnt_size < 8 ||
        fat_off > g.rom.size() || fat_size > g.rom.size() - fat_off ||
        fnt_off > g.rom.size() || fnt_size > g.rom.size() - fnt_off) {
        fs_unmount();
        return false;
    }

    g.files.resize(fat_size / 8);
    for (size_t i = 0; i < g.files.size(); ++i) {
        g.files[i].start = rd32(&g.rom[fat_off + i * 8]);
        g.files[i].end = rd32(&g.rom[fat_off + i * 8 + 4]);
        if (g.files[i].start > g.files[i].end || g.files[i].end > g.rom.size()) {
            fs_unmount();
            return false;
        }
    }

    // The root record's "first file ID" is where the named filesystem begins;
    // everything below it is an overlay.
    g.overlays = rd16(&g.rom[fnt_off + 4]);
    const uint16_t dirs = rd16(&g.rom[fnt_off + 6]);
    std::vector<bool> visited(dirs, false), named(g.files.size(), false);
    if (!dirs || dirs > 4096 || dirs * 8u > fnt_size ||
        g.overlays > static_cast<int32_t>(g.files.size()) ||
        !walk_dir(&g.rom[fnt_off], fnt_size, 0xF000, dirs, "", visited, named, 0)) {
        fs_unmount();
        return false;
    }

    g.mounted = true;
    g.from_dir = false;
    return true;
}

bool fs_mount_dir(const char *dir_path) {
    fs_unmount();
    // The manifest extract_rom.py writes alongside the files: one
    // "<id> <size> <path>" line per entry, so IDs survive extraction.
    std::string manifest = std::string(dir_path) + "/files.txt";
    std::FILE *f = std::fopen(manifest.c_str(), "rb");
    if (!f) return false;

    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        int32_t id = 0, size = 0;
        char path[768] = {};
        if (std::sscanf(line, "%d %d %767[^\r\n]", &id, &size, path) < 2) continue;
        if (id < 0) continue;
        if (static_cast<size_t>(id) >= g.files.size()) g.files.resize(id + 1);
        g.files[id].start = 0;
        g.files[id].end = static_cast<uint32_t>(size);
        g.files[id].path = path;
        if (path[0]) g.by_path[path] = id;
    }
    std::fclose(f);

    g.root = dir_path;
    g.mounted = !g.files.empty();
    g.from_dir = true;
    // Overlays are the leading run of unnamed entries.
    g.overlays = 0;
    for (size_t i = 0; i < g.files.size() && g.files[i].path.empty(); ++i)
        g.overlays = static_cast<int32_t>(i + 1);
    return g.mounted;
}

void fs_unmount() {
    g = Fs{};
}

bool fs_mounted() { return g.mounted; }
bool fs_rom_view(RomView &out) {
    if (!g.mounted || g.from_dir || g.rom.empty()) { out={nullptr,0}; return false; }
    out={g.rom.data(),static_cast<uint32_t>(g.rom.size())};
    return true;
}
int32_t fs_file_count() { return static_cast<int32_t>(g.files.size()); }
int32_t fs_overlay_count() { return g.overlays; }

int32_t fs_size(int32_t id) {
    if (id < 0 || static_cast<size_t>(id) >= g.files.size()) return -1;
    const Entry &e = g.files[id];
    return static_cast<int32_t>(e.end - e.start);
}

const char *fs_path(int32_t id) {
    if (id < 0 || static_cast<size_t>(id) >= g.files.size()) return "";
    return g.files[id].path.c_str();
}

int32_t fs_id_by_path(const char *path) {
    auto it = g.by_path.find(path);
    return it == g.by_path.end() ? -1 : it->second;
}

int32_t fs_read(int32_t id, void *dst, int32_t capacity) {
    const int32_t size = fs_size(id);
    if (size < 0 || capacity < 0 || !dst) return -1;
    const int32_t n = size < capacity ? size : capacity;

    if (g.from_dir) {
        const std::string &p = g.files[id].path;
        if (p.empty()) return -1;
        const std::string full = g.root + "/" + p;
        std::FILE *f = std::fopen(full.c_str(), "rb");
        if (!f) return -1;
        const size_t got = std::fread(dst, 1, static_cast<size_t>(n), f);
        std::fclose(f);
        return static_cast<int32_t>(got);
    }

    const Entry &e = g.files[id];
    if (e.start > g.rom.size() || static_cast<size_t>(n) > g.rom.size() - e.start) return -1;
    if (n) std::memcpy(dst, g.rom.data() + e.start, static_cast<size_t>(n));
    return n;
}

bool fs_overlay_info(uint32_t overlayId, OverlayInfo &out) {
    out = {};
    if (!g.mounted || g.from_dir || g.rom.size() < 0x58) return false;
    const uint32_t offset = rd32(g.rom.data() + 0x50), size = rd32(g.rom.data() + 0x54);
    if (!size || size % 32 || offset > g.rom.size() || size > g.rom.size() - offset) return false;
    for (uint32_t n = 0; n < size; n += 32) {
        const uint8_t *record = g.rom.data() + offset + n;
        if (rd32(record) != overlayId) continue;
        const uint32_t file = rd32(record + 24), ram = rd32(record + 4), length = rd32(record + 8);
        if (file >= g.files.size() || !length || length > 4 * 1024 * 1024 ||
            ram > UINT32_MAX - length || rd32(record + 12) > UINT32_MAX - ram - length)
            return false;
        const uint32_t flags = rd32(record + 28);
        const bool compressed = (flags & 0x01000000) != 0;
        if (compressed && (flags & 0xffffff) != g.files[file].end - g.files[file].start)
            return false;
        out = {ram, length, rd32(record + 12), file, compressed};
        return true;
    }
    return false;
}

}  // namespace ntr

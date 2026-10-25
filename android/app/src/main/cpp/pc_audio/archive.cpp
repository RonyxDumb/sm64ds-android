#include "archive.h"
#include "audio_core.h"
#include "ntr/fs.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
SdatArchive g_sdat{};
namespace {
std::vector<sd_u8> image;
size_t infoSize = 0;
struct Wave {
    std::vector<sd_s16> pcm;
    SdatWave description{};
};
std::unordered_map<const sd_u8 *, Wave> waves;
std::unordered_map<const sd_u8 *, std::array<const sd_u8 *, 4>> banks;
sd_u16 rd16(const sd_u8 *p) {
    return p[0] | (p[1] << 8);
}
sd_u32 rd32(const sd_u8 *p) {
    return sd_u32(p[0]) | (sd_u32(p[1]) << 8) | (sd_u32(p[2]) << 16) | (sd_u32(p[3]) << 24);
}
bool range(size_t at, size_t n, size_t size) {
    return at <= size && n <= size - at;
}
sd_u32 info_count(int record) {
    if (!g_sdat.info)
        return 0;
    size_t table = rd32(g_sdat.info + 8 + record * 4);
    if (!table || !range(table, 4, infoSize))
        return 0;
    sd_u32 count = rd32(g_sdat.info + table);
    return count < 65536 && range(table + 4, size_t(count) * 4, infoSize) ? count : 0;
}
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("Invalid PC audio archive bounds.");
}
int note_record(const sd_u8 *p, size_t size, SdatNote *out) {
    if (size < 10)
        return 0;
    *out = {rd16(p), rd16(p + 2), p[4], p[5], p[6], p[7], p[8], p[9]};
    return 1;
}
}
sd_u8 *sdat_file(sd_u32 id) {
    if (id >= g_sdat.fatCount)
        return nullptr;
    const sd_u8 *e = g_sdat.fat + 12 + id * 16;
    size_t off = rd32(e), size = rd32(e + 4);
    return range(off, size, image.size()) ? image.data() + off : nullptr;
}
sd_u32 sdat_file_size(sd_u32 id) {
    return id < g_sdat.fatCount ? rd32(g_sdat.fat + 16 + id * 16) : 0;
}
sd_u32 sdat_file_id_of(const void *p) {
    uintptr_t value = reinterpret_cast<uintptr_t>(p),
              base = reinterpret_cast<uintptr_t>(image.data());
    if (value < base || value - base >= image.size())
        return sd_u32(-1);
    for (sd_u32 i = 0; i < g_sdat.fatCount; i++) {
        size_t start = rd32(g_sdat.fat + 12 + i * 16), size = sdat_file_size(i);
        if (value - base >= start && value - base - start < size)
            return i;
    }
    return sd_u32(-1);
}
sd_u8 *sdat_info_entry(int record, sd_u32 id) {
    if (!g_sdat.info || record < 0 || record >= SDAT_REC_COUNT)
        return nullptr;
    size_t table = rd32(g_sdat.info + 8 + record * 4);
    if (!table || !range(table, 4, infoSize))
        return nullptr;
    sd_u32 count = rd32(g_sdat.info + table);
    if (id >= count || !range(table + 4, size_t(count) * 4, infoSize))
        return nullptr;
    size_t off = rd32(g_sdat.info + table + 4 + id * 4);
    return off && range(off, 4, infoSize) ? g_sdat.info + off : nullptr;
}
int sdat_seq_priority(const sd_u8 *base, sd_u32 start) {
    sd_u32 id = sdat_file_id_of(base);
    auto *file = sdat_file(id);
    size_t size = sdat_file_size(id);
    if (!file || size < 0x20)
        return 64;
    if (!std::memcmp(file, "SSAR", 4)) {
        sd_u32 count = rd32(file + 0x1c);
        if (!range(0x20, size_t(count) * 12, size))
            return 64;
        for (sd_u32 i = 0; i < count; i++) {
            auto *e = file + 0x20 + i * 12;
            if (rd32(e) == start)
                return e[7];
        }
    } else {
        for (sd_u32 i = 0; i < info_count(SDAT_REC_SEQ); i++) {
            auto *e = sdat_info_entry(SDAT_REC_SEQ, i);
            if (!e)
                continue;
            if (rd16(e) == id && range(size_t(e - g_sdat.info), 8, infoSize))
                return e[7];
        }
    }
    return 64;
}
int sdat_swar_wave(const sd_u8 *swar, sd_u32 index, SdatWave *out) {
    sd_u32 file = sdat_file_id_of(swar);
    size_t size = sdat_file_size(file);
    if (!swar || size < 0x3c || std::memcmp(swar, "SWAR", 4))
        return 0;
    sd_u32 count = rd32(swar + 0x38);
    if (index >= count || !range(0x3c, size_t(count) * 4, size))
        return 0;
    size_t off = rd32(swar + 0x3c + index * 4);
    if (!range(off, 12, size))
        return 0;
    auto found = waves.find(swar + off);
    if (found == waves.end())
        return 0; // all decoding happens at mount, never while mixing
    *out = found->second.description;
    return 1;
}
int sdat_link_bank_waves(sd_u8 *bank) {
    if (banks.find(bank) != banks.end())
        return 0;
    sd_u32 file = sdat_file_id_of(bank);
    auto *entry = static_cast<sd_u8 *>(nullptr);
    for (sd_u32 i = 0; i < info_count(SDAT_REC_BANK); i++) {
        auto *e = sdat_info_entry(SDAT_REC_BANK, i);
        if (!e)
            continue;
        if (rd32(e) == file) {
            entry = e;
            break;
        }
    }
    if (!entry || !range(size_t(entry - g_sdat.info), 12, infoSize))
        return 0;
    std::array<const sd_u8 *, 4> linked{};
    int count = 0;
    for (int k = 0; k < 4; k++) {
        sd_u16 id = rd16(entry + 4 + k * 2);
        if (id == 0xffff)
            continue;
        auto *e = sdat_info_entry(SDAT_REC_WAVEARC, id);
        if (e) {
            linked[k] = sdat_file(rd32(e));
            if (linked[k])
                count++;
        }
    }
    banks.emplace(bank, linked);
    return count;
}
int sdat_bank_note(const sd_u8 *bank, int program, int note, SdatNote *out, const sd_u8 **swar) {
    size_t size = sdat_file_size(sdat_file_id_of(bank));
    if (!bank || size < 0x3c || std::memcmp(bank, "SBNK", 4) || program < 0)
        return 0;
    sd_u32 count = rd32(bank + 0x38);
    if (sd_u32(program) >= count || !range(0x3c, size_t(count) * 4, size))
        return 0;
    const sd_u8 *instrument = bank + 0x3c + program * 4;
    int type = instrument[0];
    size_t off = rd16(instrument + 1);
    if (!off || !range(off, 2, size))
        return 0;
    if (type >= 1 && type <= 5) {
    } else if (type == 0x10) {
        int lo = bank[off], hi = bank[off + 1];
        if (note < lo || note > hi)
            return 0;
        off += 4 + size_t(note - lo) * 12;
    } else if (type == 0x11) {
        if (!range(off, 8, size))
            return 0;
        int region = -1;
        for (int i = 0; i < 8 && bank[off + i]; i++)
            if (note <= bank[off + i]) {
                region = i;
                break;
            }
        if (region < 0)
            return 0;
        off += 10 + region * 12;
    } else
        return 0;
    if (!range(off, 10, size) || !note_record(bank + off, size - off, out) || out->swar > 3)
        return 0;
    auto it = banks.find(bank);
    if (it == banks.end() || !it->second[out->swar])
        return 0;
    *swar = it->second[out->swar];
    return 1;
}
void sd_waves_reset() {
    waves.clear();
}
int sd_cnv_vol(int v) {
    if (v <= 0)
        return -723;
    if (v > 127)
        v = 127;
    return std::max(-723, int(200 * std::log10(double(v) / 127) - .5));
}
int out_volume_pct() {
    return 100;
}
void pc_archive_close() {
    sd_seq_reset();
    sd_mix_reset();
    waves.clear();
    banks.clear();
    g_sdat = {};
    image.clear();
    infoSize = 0;
}
void pc_archive_load() {
    int id = ntr::fs_id_by_path("data/sound_data.sdat"), size = ntr::fs_size(id);
    require(size >= 0x40 && size <= 32 * 1024 * 1024);
    std::vector<sd_u8> bytes(size);
    require(ntr::fs_read(id, bytes.data(), size) == size);
    pc_archive_load_image(std::move(bytes));
}
void pc_archive_load_image(std::vector<sd_u8> source) {
    pc_archive_close();
    require(source.size() >= 0x40 && source.size() <= 32 * 1024 * 1024);
    image = std::move(source);
    require(!std::memcmp(image.data(), "SDAT", 4));
    auto block = [&](size_t header, const char *magic, size_t minimum) {
        size_t off = rd32(image.data() + header), n = rd32(image.data() + header + 4);
        require(range(off, n, image.size()) && n >= minimum &&
                !std::memcmp(image.data() + off, magic, 4));
        return off;
    };
    size_t info = block(0x18, "INFO", 40), fat = block(0x20, "FAT ", 12);
    infoSize = rd32(image.data() + 0x1c);
    g_sdat.base = image.data();
    g_sdat.size = image.size();
    g_sdat.info = image.data() + info;
    g_sdat.fat = image.data() + fat;
    g_sdat.fatCount = rd32(g_sdat.fat + 8);
    require(g_sdat.fatCount < 65536 && range(fat + 12, size_t(g_sdat.fatCount) * 16, image.size()));
    for (sd_u32 f = 0; f < g_sdat.fatCount; f++) {
        auto *data = sdat_file(f);
        size_t n = sdat_file_size(f);
        require(data && n >= 4);
        if (n < 0x3c || std::memcmp(data, "SWAR", 4))
            continue;
        size_t count = rd32(data + 0x38);
        require(count < 65536 && range(0x3c, count * 4, n));
        for (size_t w = 0; w < count; w++) {
            size_t off = rd32(data + 0x3c + w * 4);
            require(range(off, 12, n));
            auto clip = sm64ds::decode_wave(data + off, n - off);
            Wave wave;
            size_t skip = data[off] == 2 ? 1 : 0;
            wave.pcm.reserve(clip.pcm.size() - skip);
            for (size_t i = skip; i < clip.pcm.size(); i++)
                wave.pcm.push_back(
                    static_cast<sd_s16>(std::clamp(clip.pcm[i] * 32768.f, -32768.f, 32767.f)));
            wave.description = {nullptr, sd_u32(wave.pcm.size()), clip.loop ? 1 : 0,
                                sd_u32(clip.loopStart >= skip ? clip.loopStart - skip : 0),
                                rd16(data + off + 2)};
            auto inserted = waves.emplace(data + off, std::move(wave));
            inserted.first->second.description.pcm = inserted.first->second.pcm.data();
        }
    }
    for (sd_u32 f = 0; f < g_sdat.fatCount; f++) {
        auto *file = sdat_file(f);
        if (sdat_file_size(f) >= 0x3c && !std::memcmp(file, "SBNK", 4))
            sdat_link_bank_waves(file);
    }
    sd_seq_reset();
    sd_mix_reset();
}
int pc_start_sequence(int player, int sequence) {
    auto *e = sdat_info_entry(SDAT_REC_SEQ, sequence);
    if (!e || !range(size_t(e - g_sdat.info), 8, infoSize))
        return 0;
    auto *file = sdat_file(rd32(e));
    size_t size = sdat_file_size(rd32(e));
    auto *bankInfo = sdat_info_entry(SDAT_REC_BANK, rd16(e + 4));
    if (!file || size < 0x1c || std::memcmp(file, "SSEQ", 4) || !bankInfo)
        return 0;
    size_t start = rd32(file + 0x18);
    if (start >= size)
        return 0;
    auto *bank = sdat_file(rd32(bankInfo));
    if (!bank)
        return 0;
    if (!sd_seq_start(player, file + start, 0, bank))
        return 0;
    sd_seq_set_volume(player, e[6]);
    return 1;
}
int pc_start_effect(int player, int archive, int entry, int bankOverride) {
    auto *e = sdat_info_entry(SDAT_REC_SEQARC, archive);
    if (!e)
        return 0;
    auto *file = sdat_file(rd32(e));
    size_t size = sdat_file_size(rd32(e));
    if (!file || size < 0x20 || std::memcmp(file, "SSAR", 4) || entry < 0)
        return 0;
    size_t count = rd32(file + 0x1c), data = rd32(file + 0x18);
    if (size_t(entry) >= count || !range(0x20, count * 12, size) || data >= size)
        return 0;
    const sd_u8 *record = file + 0x20 + size_t(entry) * 12;
    size_t start = rd32(record);
    if (start >= size - data)
        return 0;
    int bankId = bankOverride >= 0 ? bankOverride : rd16(record + 4);
    auto *bankInfo = sdat_info_entry(SDAT_REC_BANK, bankId);
    if (!bankInfo)
        return 0;
    auto *bank = sdat_file(rd32(bankInfo));
    if (!bank || !sd_seq_start(player, file + data, sd_u32(start), bank))
        return 0;
    sd_seq_set_volume(player, record[6]);
    return 1;
}

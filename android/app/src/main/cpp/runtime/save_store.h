#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace sm64ds {
// The DS cartridge's 8 KiB EEPROM. Record defaults, flags, checksums and mirror
// selection are implemented by the recovered SaveData methods, not this class.
class SaveStore {
public:
    static constexpr size_t Size = 0x2000, FileSize = 0x44, MinigameSize = 0x2e4;
    struct alignas(4) FileData : std::array<uint8_t, FileSize> {};
    struct alignas(4) MinigameData : std::array<uint8_t, MinigameSize> {};
    explicit SaveStore(std::string path);
    ~SaveStore();
    SaveStore(const SaveStore &) = delete;
    SaveStore &operator=(const SaveStore &) = delete;
    bool load_file(unsigned slot, FileData &data);
    bool save_file(unsigned slot, FileData &data);
    bool load_minigames(MinigameData &data);
    bool save_minigames(MinigameData &data);
    bool read(uint32_t offset, void *data, uint32_t size) const;
    bool write(uint32_t offset, const void *data, uint32_t size);
    int take_lock();
    void release_lock(int id);
    void claim(int id);
    void unclaim(int id);
    static SaveStore &active();

private:
    std::string path_;
    std::array<uint8_t, Size> image_;
    std::mutex recordMutex_;
    bool locked_ = false, claimed_ = false;
    bool persist(const std::array<uint8_t, Size> &image) const;
};
}

#include "save_store.h"
#include "SaveData.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace {
std::atomic<sm64ds::SaveStore *> current{nullptr};
bool span(uint32_t offset, uint32_t size) {
    return offset <= sm64ds::SaveStore::Size && size <= sm64ds::SaveStore::Size - offset;
}
}

// Native storage for the pointer-free save globals. The cartridge record is
// kept verbatim; native pointers are never written into these buffers.
extern "C" {
alignas(16) int data_0209caa0[0x32c / 4]{};
asm(".global data_0209cab4\n.set data_0209cab4, data_0209caa0 + 0x14\n"
    ".global data_0209cad2\n.set data_0209cad2, data_0209caa0 + 0x32\n"
    ".global data_0209cae4\n.set data_0209cae4, data_0209caa0 + 0x44\n");
uint8_t data_020a4b40[8] = {'d', 's', ' ', 'm', 'a', 'r', 'i', 'o'};
// CpuCopy8.c reads r0 and writes r1. Some recovered declarations call the
// parameters dst/src in the opposite order; preserve the primitive's behavior.
void CpuCopy8(const void *source, void *target, uint32_t size) {
    if (size && (!source || !target))
        throw std::runtime_error("Invalid recovered save copy.");
    if (size)
        std::memcpy(target, source, size);
}
void func_0205a588(void *dest, int value, int size) {
    if (size < 0 || (!dest && size))
        throw std::runtime_error("Invalid recovered save clear.");
    std::memset(dest, value, static_cast<size_t>(size));
}
int func_0203da3c() {
    return 0;
} // local cartridge, no wireless peer session
uint32_t func_0206045c() {
    return sm64ds::SaveStore::Size;
}
int func_02057020() {
    return sm64ds::SaveStore::active().take_lock();
}
void func_02057078(uint16_t id) {
    sm64ds::SaveStore::active().release_lock(id);
}
void func_0205ff80(uint16_t id) {
    sm64ds::SaveStore::active().claim(id);
}
void func_0205ff70(uint16_t id) { sm64ds::SaveStore::active().unclaim(id); }
int func_02060558(uint32_t offset, void *data, uint32_t size, int, int, int) {
    return sm64ds::SaveStore::active().read(offset, data, size) ? 1 : 0;
}
int func_02060484(uint32_t offset, const void *data, uint32_t size, int, int, int) {
    return sm64ds::SaveStore::active().write(offset, data, size) ? 1 : 0;
}
}

namespace sm64ds {
SaveStore::SaveStore(std::string path) : path_(std::move(path)) {
    image_.fill(0xff);
    FILE *file = std::fopen(path_.c_str(), "rb");
    if (file) {
        const size_t count = std::fread(image_.data(), 1, image_.size(), file);
        const bool extra = std::fgetc(file) != EOF;
        const bool failed = std::ferror(file) != 0;
        std::fclose(file);
        if (count != Size || extra || failed)
            throw std::runtime_error("Save image is not exactly 8192 bytes; file preserved.");
    } else if (errno != ENOENT) {
        throw std::runtime_error("Could not open the cartridge save image.");
    }
    SaveStore *expected = nullptr;
    if (!current.compare_exchange_strong(expected, this))
        throw std::runtime_error("A cartridge save session is already active.");
    std::memset(data_0209caa0, 0, sizeof data_0209caa0);
    // The shared minigame flags are the mask ReadFileData applies at +0x48.
    // Boot reads that record first. A blank EEPROM takes the original defaults.
    SaveData::ReadMinigameData(reinterpret_cast<MinigameSaveData *>(data_0209caa0 + 0x11));
}
SaveStore::~SaveStore() {
    SaveStore *expected = this;
    current.compare_exchange_strong(expected, nullptr);
}
SaveStore &SaveStore::active() {
    auto *store = current.load();
    if (!store)
        throw std::runtime_error("The cartridge save medium is not open.");
    return *store;
}
int SaveStore::take_lock() {
    recordMutex_.lock();
    locked_ = true;
    return 1;
}
void SaveStore::release_lock(int id) {
    if (id != 1 || !locked_ || claimed_)
        throw std::runtime_error("Invalid cartridge lock release.");
    locked_ = false;
    recordMutex_.unlock();
}
void SaveStore::claim(int id) {
    if (id != 1 || !locked_ || claimed_)
        throw std::runtime_error("Invalid cartridge claim.");
    claimed_ = true;
}
void SaveStore::unclaim(int id) {
    if (id != 1 || !locked_ || !claimed_)
        throw std::runtime_error("Invalid cartridge unclaim.");
    claimed_ = false;
}
bool SaveStore::read(uint32_t offset, void *data, uint32_t size) const {
    if (!claimed_ || !data || !span(offset, size))
        return false;
    std::memcpy(data, image_.data() + offset, size);
    return true;
}
bool SaveStore::write(uint32_t offset, const void *data, uint32_t size) {
    if (!claimed_ || !data || !span(offset, size))
        return false;
    auto next = image_;
    std::memcpy(next.data() + offset, data, size);
    if (!persist(next))
        return false;
    image_ = next;
    return true;
}
bool SaveStore::persist(const std::array<uint8_t, Size> &image) const {
    // Same raw EEPROM image and temporary-file replacement as the PC port,
    // with fsync before rename so Android process death cannot truncate a save.
    const std::string temp = path_ + ".pending";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    size_t done = 0;
    while (done < Size) {
        const ssize_t count = ::write(fd, image.data() + done, Size - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        done += static_cast<size_t>(count);
    }
    bool ok = done == Size && ::fsync(fd) == 0;
    if (::close(fd) != 0)
        ok = false;
    if (ok)
        ok = ::rename(temp.c_str(), path_.c_str()) == 0;
    if (!ok)
        ::unlink(temp.c_str());
    return ok;
}
bool SaveStore::load_file(unsigned slot, FileData &data) {
    if (slot >= 3)
        return false;
    return SaveData::ReadFileData(slot, reinterpret_cast<FileSaveData *>(data.data())) == 1;
}
bool SaveStore::save_file(unsigned slot, FileData &data) {
    if (slot >= 3)
        return false;
    return SaveData::SaveFile(slot, reinterpret_cast<FileSaveData *>(data.data())) == 1;
}
bool SaveStore::load_minigames(MinigameData &data) {
    return SaveData::ReadMinigameData(reinterpret_cast<MinigameSaveData *>(data.data())) == 1;
}
bool SaveStore::save_minigames(MinigameData &data) {
    return SaveData::SaveMinigames(reinterpret_cast<MinigameSaveData *>(data.data())) == 1;
}
}

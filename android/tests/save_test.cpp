#include "runtime/save_store.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

using sm64ds::SaveStore;
static uint16_t checksum(const uint8_t *data, size_t size) {
    uint16_t sum = 0;
    for (char c : std::string("ds mario"))
        sum += static_cast<uint8_t>(c);
    for (size_t i = 0; i < size; ++i)
        sum = static_cast<uint16_t>((sum << 1) | (sum >> 15)) ^ data[i];
    return sum;
}
static void corrupt(const std::string &path, int offset) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(offset);
    f.put(0x7f);
    assert(f.good());
}
int main() {
    char directory[] = "/tmp/sm64ds-save-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/game.sav";
    SaveStore::FileData file{};
    SaveStore::MinigameData mini{};
    {
        SaveStore store(path);
        assert(store.load_file(0, file));
        assert(file[0] == '8' && file[1] == '0' && file[0x41] == 3 && file[8] == 8);
        assert(access(path.c_str(), F_OK) != 0); // startup does not write a blank game
        assert(!store.load_file(3, file));
        file[0x13] = 0xa5;
        assert(store.save_file(0, file));
        assert(store.load_minigames(mini) && mini[0] == '5');
        mini[12] = 0x4a;
        assert(store.save_minigames(mini));
        assert(!store.read(UINT32_MAX, file.data(), 2));
        assert(!store.write(0, file.data(), 2)); // card has not been claimed
    }
    {
        std::ifstream f(path, std::ios::binary);
        const std::vector<uint8_t> image{std::istreambuf_iterator<char>(f), {}};
        assert(image.size() == SaveStore::Size);
        assert(std::equal(image.begin(), image.begin() + 0x4e, image.begin() + 0x1000));
        assert(std::string(image.begin() + 2, image.begin() + 10) == "ds mario");
        const uint16_t sum = image[0] | (uint16_t(image[1]) << 8);
        assert(sum == checksum(image.data() + 10, SaveStore::FileSize));
    }
    {
        SaveStore store(path);
        assert(store.load_file(0, file) && file[0x13] == 0xa5);
        assert(store.load_minigames(mini) && mini[12] == 0x4a);
        const int lock = store.take_lock();
        store.claim(lock);
        assert(!store.read(UINT32_MAX, file.data(), 2));
        assert(!store.write(SaveStore::Size - 1, file.data(), 2));
        store.unclaim(lock);
        store.release_lock(lock);
    }
    corrupt(path, 0); // original code must recover the mirror
    {
        SaveStore store(path);
        assert(store.load_file(0, file) && file[0x13] == 0xa5);
    }
    corrupt(path, 0x1000);
    {
        SaveStore store(path);
        assert(!store.load_file(0, file));
        assert(file[0] == '8' && file[0x13] == 0); // original recovery defaults
    }
    {
        // A provider/storage failure must propagate through the original retry
        // logic, and a failed write must not alter the medium in memory.
        SaveStore store(std::string(directory) + "/missing/game.sav");
        assert(store.load_file(0, file));
        file[0x13] = 0x55;
        assert(!store.save_file(0, file));
        assert(store.load_file(0, file) && file[0x13] == 0);
    }
    { std::ofstream f(path, std::ios::trunc); f << "bad"; }
    bool rejected = false;
    try { SaveStore store(path); } catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    { std::ifstream f(path); std::string data; f >> data; assert(data == "bad"); }
    std::remove(path.c_str());
    rmdir(directory);
    std::puts("Recovered saves: defaults, records/checksums, mirror recovery, reopen, I/O failure passed.");
}

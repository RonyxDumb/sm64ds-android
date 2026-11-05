#include "runtime/save_store.h"
#include "runtime/minigame_progress.h"
#include "runtime/minigame_controls.h"
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
    // Keep the original save-test's 0x4a record unchanged. The live
    // minigame globals and a separately supplied SaveStore::MinigameData
    // are two distinct owners until the menu explicitly loads one into the
    // other. Exercise their persistence on an independent cartridge image.
    const std::string minigamePath = std::string(directory) + "/minigame.sav";
    {
        SaveStore store(minigamePath);
        using sm64ds::MinigameProgress;
        assert(MinigameProgress::unlocked(0));
        using sm64ds::MinigameMenuControls;
        using sm64ds::MinigameTapKind;
        assert(!MinigameMenuControls::has_second_page());
        const auto first = MinigameMenuControls::tap(0x45, 0x20, 0, 0);
        assert(first.kind == MinigameTapKind::Select && first.index == 0);
        const auto locked = MinigameMenuControls::tap(0xa5, 0x20, 0, 0);
        assert(locked.kind == MinigameTapKind::None);
        assert(MinigameMenuControls::tap(0x12, 0x65, 0, 0).kind == MinigameTapKind::Group);
        assert(MinigameMenuControls::tap(0xe0, 0xa0, 0, 0).kind == MinigameTapKind::Exit);
        assert(MinigameMenuControls::tap(0x45, 0x20, 0, 0, false).kind == MinigameTapKind::None);
        assert(!MinigameProgress::unlocked(35));
        MinigameProgress::unlock(35);
        MinigameProgress::unlock(24);
        assert(MinigameProgress::unlocked(35));
        assert(MinigameMenuControls::has_second_page());
        assert(MinigameMenuControls::tap(0xe0, 0x60, 0, 0).kind ==
               MinigameTapKind::NextPage);
        assert(MinigameMenuControls::tap(0x40, 0x60, 1, 0).kind ==
               MinigameTapKind::PreviousPage);
        const auto second = MinigameMenuControls::tap(0x65, 0x60, 1, 0);
        assert(second.kind == MinigameTapKind::Select && second.index == 24);
        assert(MinigameProgress::is_new(0));
        assert(MinigameProgress::is_new(35));
        MinigameProgress::clear_new(0);
        MinigameProgress::clear_new(35);
        assert(!MinigameProgress::is_new(0));
        assert(!MinigameProgress::is_new(35));
        MinigameProgress::set_record(35, 4, 4321);
        assert(MinigameProgress::record(35, 4) == 4321);
        assert(MinigameProgress::record(0, 0) == 0);
        bool invalid = false;
        try { (void)MinigameProgress::record(36, 0); }
        catch (const std::out_of_range &) { invalid = true; }
        assert(invalid);
        invalid = false;
        try { MinigameProgress::unlock(36); }
        catch (const std::out_of_range &) { invalid = true; }
        assert(invalid);
        assert(MinigameProgress::persist());
    }
    {
        SaveStore store(minigamePath);
        using sm64ds::MinigameProgress;
        assert(MinigameProgress::unlocked(35) && !MinigameProgress::is_new(35));
        assert(!MinigameProgress::is_new(0));
        assert(MinigameProgress::record(35, 4) == 4321);
    }
    std::remove(minigamePath.c_str());
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

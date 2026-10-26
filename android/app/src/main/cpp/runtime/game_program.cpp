#include "game_program.h"
#include "boot_seams.h"
#include "resources.h"
#include "ntr/lz77.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace {
sm64ds::GameProgram *program = nullptr;
constexpr u16 NoScene = 0x187;
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
const char *single_program_path(const char *path) {
    // Reject a second session before member constructors reset shared game
    // globals belonging to the live session.
    require(!program, "A native game program is already running.");
    return path;
}
uint32_t word(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool video_span(const void *p, size_t size, uintptr_t base, size_t capacity) {
    uintptr_t at = reinterpret_cast<uintptr_t>(p);
    return at >= base && at - base <= capacity && size <= capacity - (at - base);
}
void copy_video(const void *source, void *target, size_t size) {
    require(source && (video_span(target, size, ntr::VRAM_BASE, ntr::VRAM_SIZE) ||
                       video_span(target, size, ntr::PLTT_BASE, ntr::PLTT_SIZE)),
            "Video upload outside mapped memory.");
    require(size <= sm64ds::GameProgram::active().bytes_at(source),
            "Video upload exceeds source data.");
    std::memcpy(target, source, size);
}
}
extern "C" {
dFdColor_c data_0209f5e8;
dFdBrightness_c *data_0209f5bc = &data_0209f5e8, *data_0209d4ac = &data_0209f5e8;
u16 data_02092664 = NoScene;
u32 data_0209f5b8 = 0;
u8 data_0209f1e8 = 0, data_0209d454 = 0, data_0209d45c = 0;
int data_0208ee44 = 1;
alignas(4) u8 data_020914e0[0x48], data_02091570[0x168], data_020916d8[0x1ec], data_020918c4[0x84c],
    data_0209d3ac[2]{};
short data_0209d468[4]{}, data_0209d46c[4]{}, data_0209d494[4]{}, data_0209d498[4]{};
short data_0209d484[4]{}, data_0209d480[4]{}, data_0209d47c[4]{}, data_0209d470[4]{};
u8 data_0209ee90[838]{}, data_020a0f00 = 4, data_0209d698 = 0, data_0209d660 = 0;
short data_0209d6d4 = -1;
void *data_0209d6f8 = nullptr;
u8 *data_0209d6e8 = nullptr, *data_0209d6ec = nullptr, *data_0209d6fc = nullptr,
   *data_0209d708 = nullptr, *data_0209d70c = nullptr;
void _ZN2GX15SetGraphicsModeEiii(int display, int bg, int bg0) {
    require(display >= 0 && display <= 3 && bg >= 0 && bg <= 6 && (bg0 == 0 || bg0 == 1),
            "Unsupported native graphics mode.");
    ntr::io_write(
        0x04000000,
        (ntr::io_read(0x04000000, 4) & ~0x3000fu) | (uint32_t(display) << 16) | bg | (bg0 << 3), 4);
}
void _ZN3GXS15SetGraphicsModeEi(int mode) {
    require(mode >= 0 && mode <= 5, "Unsupported sub-screen mode.");
    ntr::io_write(0x04001000, (ntr::io_read(0x04001000, 4) & ~7u) | mode, 4);
}
void _ZN2GX6DispOnEv() {
    ntr::io_write(0x04000000, ntr::io_read(0x04000000, 4) | 0x10000, 4);
}
void DecompressLZ16(const void *source, void *target) {
    auto *data = static_cast<const uint8_t *>(source);
    size_t available = sm64ds::GameProgram::active().bytes_at(source);
    require(available <= INT32_MAX, "LZ source too large.");
    int32_t size = ntr::lz77_size(data, static_cast<int32_t>(available));
    require(size > 0 && size <= 0x20000, "Invalid video LZ size.");
    std::vector<uint8_t> decoded(static_cast<size_t>(size));
    require(ntr::lz77_unpack(data, static_cast<int32_t>(available), decoded.data(), size) == size,
            "Video decompression failed.");
    require(video_span(target, size, ntr::VRAM_BASE, ntr::VRAM_SIZE),
            "LZ destination outside VRAM.");
    std::memcpy(target, decoded.data(), decoded.size());
}
void *LoadFile(int handle) {
    require(handle >= 0, "Negative cartridge handle.");
    return sm64ds::GameProgram::active().load(handle);
}
void Deallocate(void *p) {
    sm64ds::GameProgram::active().release(p);
}
int LoadFileAt(int handle, void *target) {
    void *p = LoadFile(handle);
    try {
        copy_video(p, target, sm64ds::GameProgram::active().bytes_at(p));
    } catch (...) {
        Deallocate(p);
        throw;
    }
    Deallocate(p);
    return 1;
}
u32 LoadCompressedFileAt(u16 handle, void *target) {
    void *p = LoadFile(handle);
    try {
        DecompressLZ16(p, target);
    } catch (...) {
        Deallocate(p);
        throw;
    }
    u32 size = static_cast<u32>(
        ntr::lz77_size(static_cast<uint8_t *>(p),
                       static_cast<int32_t>(sm64ds::GameProgram::active().bytes_at(p))));
    Deallocate(p);
    return size;
}
void MultiCopy_Int(int *source, int *target, int size) {
    require(size >= 0, "Negative video copy length.");
    copy_video(source, target, static_cast<size_t>(size));
}
void *Mg_GetWorkBuffer() {
    throw std::runtime_error("Minigame font work buffer is not linked.");
}
int func_0201a244(AsyncJobCallback callback, int argument, int, int stack, int) {
    require(stack == 0 && callback, "Unsupported native boot stack.");
    sm64ds::GameProgram::active().queue(callback, argument);
    return 0;
}
int func_0201a1bc() {
    return sm64ds::GameProgram::active().worker_complete() ? 1 : 0;
}
void func_0201a2f8(int) {
    sm64ds::GameProgram::active().prepare_boot();
}
void func_0201cd08(int) {
    throw std::runtime_error("Save-erasure message rendering is not linked.");
}
u32 func_02012790(u32 entry) {
    return sm64ds::GameProgram::active().sound(2, entry) ? 1 : 0;
}
void _ZN6Memory16operator_delete2EPv(void *p) {
    sm64ds::Resources::active().heap().deallocate(p);
}
void func_02053a90(u16 *reg, int value) {
    value = std::clamp(value, -16, 16);
    *reg = static_cast<u16>((value < 0 ? 0x8000 : value > 0 ? 0x4000 : 0) | std::abs(value));
}
void _ZN3G2x18SetBlendBrightnessEPVtts(volatile u16 *reg, u16 targets, int value) {
    value = std::clamp(value, -16, 16);
    reg[0] = static_cast<u16>(targets | (value < 0 ? 0xc0 : 0x80));
    reg[2] = static_cast<u16>(std::abs(value));
}
}
namespace Message {
void DisplaySaveStatusText(u32) {
    throw std::runtime_error("Save-erasure message rendering is not linked.");
}
}
namespace Sound {
void Play2D(u32 a, u32 b) {
    sm64ds::GameProgram::active().sound(a, b);
}
}
namespace GX {
void DisableAllBanks() {
    std::memset(ntr::host_ptr(0x04000240), 0, 9);
}
void SetBankForBG(u16 mask) {
    require(mask == 1, "Boot BG bank configuration changed.");
    ntr::io_write(0x04000240, 0x81, 1);
}
void SetBankForOBJ(u16 mask) {
    require(mask == 2, "Boot OBJ bank configuration changed.");
    ntr::io_write(0x04000241, 0x82, 1);
}
void SetBankForSubBG(u16 mask) {
    require(mask == 4, "Boot sub-BG bank configuration changed.");
    ntr::io_write(0x04000242, 0x84, 1);
}
void SetBankForSubOBJ(u16 mask) {
    require(mask == 8, "Boot sub-OBJ bank configuration changed.");
    ntr::io_write(0x04000243, 0x84, 1);
}
void LoadBGPltt(const void *p, u32 offset, u32 size) {
    require(offset <= 0x200 && size <= 0x200 - offset, "Main palette overflow.");
    copy_video(p, ntr::host_ptr(0x05000000 + offset), size);
}
}
namespace GXS {
void LoadBGPltt(const void *p, u32 offset, u32 size) {
    require(offset <= 0x200 && size <= 0x200 - offset, "Sub palette overflow.");
    copy_video(p, ntr::host_ptr(0x05000400 + offset), size);
}
}
extern "C" void LoadFont(u8);
namespace sm64ds {
GameProgram &GameProgram::active() {
    require(program, "No native game program.");
    return *program;
}
GameProgram::GameProgram(const char *path, Sound sound)
    : cartridge_(single_program_path(path)), stage_(cartridge_), sound_(std::move(sound)) {
    require(!program && bool(sound_), "A native game program is already running.");
    require(ntr::io_init(), "Cannot map native video registers.");
    program = this;
    try {
        std::memset(ntr::host_ptr(ntr::IO_BASE), 0, ntr::IO_SIZE);
        std::memset(ntr::host_ptr(ntr::VRAM_BASE), 0, ntr::VRAM_SIZE);
        std::memset(ntr::host_ptr(ntr::PLTT_BASE), 0, ntr::PLTT_SIZE);
        for (auto b :
             {std::make_pair(0x020914e0u, std::make_pair(data_020914e0, sizeof data_020914e0)),
              std::make_pair(0x02091570u, std::make_pair(data_02091570, sizeof data_02091570)),
              std::make_pair(0x020916d8u, std::make_pair(data_020916d8, sizeof data_020916d8)),
              std::make_pair(0x020918c4u, std::make_pair(data_020918c4, sizeof data_020918c4))}) {
            auto bytes = cartridge_.arm9(b.first, static_cast<uint32_t>(b.second.second));
            std::memcpy(b.second.first, bytes.data(), bytes.size());
        }
        data_0209f1e8 = data_0209d454 = data_0209d45c = 0;
        data_0208ee44 = 1;
        data_02092664 = NoScene;
        data_0209f5b8 = 0;
        data_0209f5bc = data_0209d4ac = &data_0209f5e8;
        data_0209f5e8.currInterp = 0x1000;
        data_0209f5e8.speed = 0;
        data_0209f5e8.color = 0;
        require(boot_.InitResources() == 1, "Recovered boot initialization failed.");
        ntr::io_write(0x04000050, 0xbf, 2);
        ntr::io_write(0x04000054, 16, 2);
        ntr::io_write(0x04001050, 0xbf, 2);
        ntr::io_write(0x04001054, 16, 2);
        data_0209f5e8.SetBackwardTime(30);
        scanout();
    } catch (...) {
        while (!files_.empty())
            release(files_.begin()->first);
        program = nullptr;
        throw;
    }
}
GameProgram::~GameProgram() {
    job_ = nullptr;
    frontend_.reset();
    title_.reset();
    while (!files_.empty())
        release(files_.begin()->first);
    data_0209d6e8 = data_0209d6ec = data_0209d6fc = data_0209d708 = data_0209d70c = nullptr;
    data_0209d6f8 = nullptr;
    if (program == this)
        program = nullptr;
}
void GameProgram::prepare_boot() {
    // Native card/catalog/font seam for the worker. Session already owns the
    // resolved resource catalog and sequencer; no DS overlay code is executed.
    require(Resources::active().handle_count() == 2058, "Incomplete native resource catalog.");
    LoadFont(0);
}
void GameProgram::queue(void (*callback)(int), int argument) {
    require(!job_, "Boot worker already occupied.");
    complete_ = false;
    job_ = callback;
    argument_ = argument;
}
void *GameProgram::load(uint32_t handle) {
    auto bytes = cartridge_.file(handle);
    if (handle >= 0x431 && handle <= 0x435) {
        require(bytes.size() >= 0x38 && !std::memcmp(bytes.data(), "GSEM1gmb", 8) &&
                    !std::memcmp(bytes.data() + 0x20, "1FNI", 4),
                "Invalid message bank.");
        uint32_t section = word(bytes.data() + 0x24);
        require(section >= 16 && section <= bytes.size() - 0x28, "Invalid message section extent.");
    }
    void *p = Resources::active().heap().allocate(static_cast<uint32_t>(bytes.size()), 32);
    require(p, "Native game heap is full.");
    try {
        files_.emplace(p, bytes.size());
    } catch (...) {
        Resources::active().heap().deallocate(p);
        throw;
    }
    std::memcpy(p, bytes.data(), bytes.size());
    return p;
}
void GameProgram::release(void *p) {
    if (!p)
        return;
    auto found = files_.find(p);
    require(found != files_.end(), "Unowned native file release.");
    Resources::active().heap().deallocate(p);
    files_.erase(found);
}
size_t GameProgram::bytes_at(const void *p) const {
    uintptr_t at = reinterpret_cast<uintptr_t>(p);
    for (auto b : files_) {
        uintptr_t begin = reinterpret_cast<uintptr_t>(b.first);
        if (at >= begin && at - begin < b.second)
            return b.second - (at - begin);
    }
    for (auto b : {std::make_pair(data_020914e0, sizeof data_020914e0),
                   std::make_pair(data_02091570, sizeof data_02091570),
                   std::make_pair(data_020916d8, sizeof data_020916d8),
                   std::make_pair(data_020918c4, sizeof data_020918c4),
                   std::make_pair(data_0209d3ac, sizeof data_0209d3ac)}) {
        uintptr_t begin = reinterpret_cast<uintptr_t>(b.first);
        if (at >= begin && at - begin < b.second)
            return b.second - (at - begin);
    }
    throw std::runtime_error("Video upload uses an unowned source.");
}
uint16_t GameProgram::requested_scene() const {
    return data_02092664;
}
bool GameProgram::accept_title_result(int result) {
    require(title_ != nullptr, "Title file records have not been prepared.");
    const bool started = title_->start_selected(result);
    if (started)
        stage_.prepare_input();
    return started;
}
void GameProgram::frame(double seconds, uint32_t keys, float x, float y, int tx, int ty) {
    require(std::isfinite(seconds) && seconds >= 0, "Invalid native frame interval.");
    constexpr double Step = 1.0 / 59.8261;
    accumulated_ += std::min(seconds, .25);
    unsigned count = 0;
    while (accumulated_ >= Step && count++ < 8) {
        accumulated_ -= Step;
        tick(keys, x, y, tx, ty);
    }
}
void GameProgram::tick(uint32_t keys, float x, float y, int tx, int ty) {
    input_.update(keys, x, y, tx, ty, data_02092664 != 3);
    if (data_02092664 == 3)
        stage_.update_input(0, input_.x(), input_.y());
    if (job_) {
        auto callback = job_;
        job_ = nullptr;
        callback(argument_);
        complete_ = true;
    }
    if (data_02092664 != NoScene) {
        if (data_0209f5bc->IsAtStart())
            data_0209f5bc->SetForwardTime(30);
        if (data_0209f5bc->IsAtEnd()) {
            if (data_02092664 == 1) {
                if (!title_)
                    title_ = std::make_unique<TitleFiles>(data_0209d6fc);
                if (!frontend_)
                    frontend_ = std::make_unique<FrontendCore>(Resources::active().heap(),
                                                               cartridge_.overlay_data(7), GetOwnerLanguage(),
                        [this](uint16_t handle) { return cartridge_.file(handle); });
                frontend_->sample(input_.raw_buttons(), tx, ty);
                throw std::runtime_error(
                    "Il core originale del frontend è inizializzato. Il dispatcher e il rendering di "
                    "dScDSMT_c non sono ancora collegati.");
            }
            if (data_02092664 == 3)
                throw std::runtime_error(
                    "StartFile originale completato. La scena di gioco dScStage_c e il runtime "
                    "degli attori non sono ancora collegati.");
            throw std::runtime_error("Scena originale non collegata: " +
                                     std::to_string(data_02092664));
        }
    }
    boot_.Behavior();
    data_0209f5bc->AdvanceFade();
    ++ticks_;
    scanout();
}
void GameProgram::scanout() {
    ntr::io_write(0x04000000,
                  (ntr::io_read(0x04000000, 4) & ~0xf00u) | (uint32_t(data_0209d45c & 15) << 8), 4);
    ntr::io_write(0x04001000,
                  (ntr::io_read(0x04001000, 4) & ~0xf00u) | (uint32_t(data_0209d454 & 15) << 8), 4);
    ntr::ppu_scanout(ntr::ENGINE_A, main_);
    ntr::ppu_scanout(ntr::ENGINE_B, sub_);
}
}

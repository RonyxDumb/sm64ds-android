#include "game_program.h"
#include "boot_seams.h"
#include "resources.h"
#include "ntr/lz77.h"
#include "ntr/gx.h"
#include "ntr/bmd.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#if defined(__ANDROID__)
#include <android/log.h>
#include <cstdio>
#include "crash_diagnostics.h"
#endif
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
// Recovered DS graphics routines sometimes return a logical 32-bit video
// address, while native backend callers can already hold a translated pointer.
// Validate both forms before touching the buffer. Android may relocate each
// video window independently if its original low VA is occupied.
void *video_destination(void *target, size_t size, uintptr_t dsBase, size_t capacity) {
    const uintptr_t at = reinterpret_cast<uintptr_t>(target);
    const uintptr_t hostBase =
        reinterpret_cast<uintptr_t>(ntr::host_ptr(static_cast<uint32_t>(dsBase)));
    const auto in_bounds = [size, capacity](uintptr_t address, uintptr_t base) {
        return address >= base && address - base <= capacity &&
               size <= capacity - (address - base);
    };
    if (in_bounds(at, hostBase)) return target;
    if (in_bounds(at, dsBase))
        return ntr::host_ptr(static_cast<uint32_t>(at));
    return nullptr;
}
void copy_video(const void *source, void *target, size_t size) {
    void *destination = video_destination(target, size, ntr::VRAM_BASE, ntr::VRAM_SIZE);
    if (!destination)
        destination = video_destination(target, size, ntr::PLTT_BASE, ntr::PLTT_SIZE);
    require(source && destination, "Video upload outside mapped memory.");
    require(size <= sm64ds::GameProgram::active().bytes_at(source),
            "Video upload exceeds source data.");
    std::memcpy(destination, source, size);
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
    void *destination = video_destination(target, static_cast<size_t>(size),
                                          ntr::VRAM_BASE, ntr::VRAM_SIZE);
    require(destination, "LZ destination outside VRAM.");
    std::memcpy(destination, decoded.data(), decoded.size());
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
void StopLoadedMusic_Layer1(unsigned frames) {
    sm64ds::GameProgram::active().native_audio({sm64ds::SoundOp::Stop,0,0,static_cast<int>(frames)});
}
}
extern "C" int func_ov007_020cc118(int music,unsigned frames) {
    return sm64ds::GameProgram::active().defer_music(music,frames);
}
extern "C" void SetSoundMode(int mode) {
    data_0209d4b4=static_cast<uint8_t>(mode);
    sm64ds::GameProgram::active().native_audio({sm64ds::SoundOp::Mode,0,0,mode==0?1:mode==1?2:0});
}
extern "C" void TurnBacklightOn() { data_0208ee3c=1; }
extern "C" void TurnBacklightOff() { data_0208ee3c=0; }
namespace GX {
void DisableAllBanks() {
    ntr::io_write(0x04000240,0,4);
    ntr::io_write(0x04000244,0,4);
    ntr::io_write(0x04000248,0,2);
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
GameProgram::GameProgram(const char *path, Sound sound,FrontendCore::Audio audio)
    : cartridge_(single_program_path(path)), stage_(cartridge_), sound_(std::move(sound)),audio_(std::move(audio)) {
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
    if (started) {
        stage_.prepare_scene();
        stage_.prepare_input();
    }
    return started;
}
int GameProgram::native_audio(const SoundRequest &request) {
    if (!audio_) throw std::runtime_error("No native scene audio control backend.");
    return audio_(request);
}
int GameProgram::defer_music(int music,unsigned frames) {
    if (pendingMusic_>=0) return 0;
    pendingMusic_=music; musicDelay_=static_cast<uint16_t>(frames);
    native_audio({SoundOp::Stop,0,0,static_cast<int>(frames)});
    return 1;
}
void GameProgram::frame(double seconds, uint32_t keys, float x, float y, int tx, int ty) {
    require(std::isfinite(seconds) && seconds >= 0, "Invalid native frame interval.");
    constexpr double Step = 1.0 / 59.8261;
    accumulated_ += std::min(seconds, .25);
    // A slow phone can accumulate several simulation steps per Android
    // callback. Do not rasterise/upload both 256x192 LCDs for every catch-up
    // step: only the final state is visible. Also cap debt so one slow frame
    // does not trigger an endless catch-up/rasterisation feedback loop.
    unsigned count = 0;
    batchFrame_ = true;
    try {
        while (accumulated_ >= Step && count < 4) {
            accumulated_ -= Step;
            ++count;
            tick(keys, x, y, tx, ty);
        }
    } catch (...) {
        batchFrame_ = false;
        throw;
    }
    batchFrame_ = false;
    if (count) {
        accumulated_ = std::min(accumulated_, Step * 2);
        scanout();
    }
}
void GameProgram::tick(uint32_t keys, float x, float y, int tx, int ty) {
    input_.update(keys, x, y, tx, ty, activeScene_ != 3 && data_02092664 != 3);
    if (activeScene_ == 3 || data_02092664 == 3)
        stage_.update_input(0, input_.x(), input_.y());
    if (job_) {
        auto callback = job_;
        job_ = nullptr;
        callback(argument_);
        complete_ = true;
    }
    if (activeScene_ == 3 && data_02092664 == NoScene) {
        // This bridge consumes the recovered controller state and cartridge KCL.
        // Actual Player/dCamera_c::Behavior and actor vtables remain unlinked.
        if (stagePilotReady_) {
            stagePilot_.update(stageCollision_, input_.x(), input_.y(),
                               (input_.pressed() & 1u) != 0);
            stageCamera_.follow(stagePilot_);
        }
        ++ticks_;
        if (!batchFrame_) scanout();
        return;
    }
    if (activeScene_==1 && data_02092664==NoScene) {
        if (pendingMusic_>=0 && (!musicDelay_ || --musicDelay_==0)) {
            native_audio({SoundOp::Music,0,0,pendingMusic_}); pendingMusic_=-1;
        }
        const int result=frontend_->update(input_.raw_buttons(),tx,ty);
        frontend_->render();
        accept_title_result(result);
        ++ticks_; if (!batchFrame_) scanout(); return;
    }
    if (data_02092664 != NoScene) {
        if (data_0209f5bc->IsAtStart())
            data_0209f5bc->SetForwardTime(30);
        if (data_0209f5bc->IsAtEnd()) {
            if (data_02092664 == 1) {
                if (!title_)
                    title_ = std::make_unique<TitleFiles>(data_0209d6fc);
                if (!frontend_) {
                    auto overlay=cartridge_.overlay_data(7);
                    auto arm9=cartridge_.arm9_data(overlay.base);
                    frontend_ = std::make_unique<FrontendCore>(Resources::active().heap(),
                        std::move(overlay), GetOwnerLanguage(),
                        [this](uint16_t handle) { return cartridge_.file(handle); },std::move(arm9),
                        &title_->config(),[this](int value){return title_->command(value);},
                        [this](const SoundRequest &request){return native_audio(request);});
                }
                activeScene_=1; data_02092664=NoScene;
                const int result=frontend_->update(input_.raw_buttons(),tx,ty);
                frontend_->render(); accept_title_result(result);
                ++ticks_; if (!batchFrame_) scanout(); return;
            }
            if (data_02092664 == 3) {
                // The original StartFile and StageFlow have already resolved the
                // requested sublevel, overlay, model and KCL. This is a native
                // stage *geometry* milestone, NOT dScStage_c actor execution.
                const auto *assets = stage_.assets();
                require(assets && stage_.address_space(),
                        "Stage 3 was requested without a prepared original level.");
                ntr::BmdModel parsed;
                require(ntr::bmd_parse(assets->model(), parsed) && !parsed.dlists.empty(),
                        "Original stage BMD could not be decoded into display lists.");
                stageModel_ = std::move(parsed);
                stagePreviewReady_ = false;
                stagePilotReady_ = false;
                // The recovered game stores collision as KCL prisms, not a
                // polygon mesh. Validate and mount its original octree.
                const bool colliderReady = stageCollision_.load(assets->collision());
                bool entranceReady = false;
                try {
                    const auto actorsOverlay = cartridge_.overlay_data(2);
                    const auto request = stage_.request();
                    entranceReady = stageActors_.read(*assets, actorsOverlay,
                                                        unsigned(request.star),
                                                        unsigned(request.entrance));
                } catch (const std::exception&) {
                    // A missing object-overlay mapping must not become an
                    // unguarded ARM64 code pointer or crash stage rendering.
                }
                if (colliderReady && entranceReady) {
                    const auto& spawn = stageActors_.entrance();
                    stagePilot_.place(spawn.x, spawn.y, spawn.z, stageCollision_);
                    stageCamera_ = StageFollowCamera{};
                    stageCamera_.yaw = double(spawn.yaw) * (3.14159265358979323846 / 32768.0);
                    for (int i = 0; i < 24; ++i) stageCamera_.follow(stagePilot_);
                    stagePilotReady_ = true;
                }
                ntr::gx_reset();
                frontend_.reset();
                activeScene_ = 3;
                data_02092664 = NoScene;
#if defined(__ANDROID__)
                char message[256];
                std::snprintf(message, sizeof message,
                    "Native stage: sublevel=%d overlay=%u BMD=%u lists=%zu "
                    "KCL=%s prisms=%zu actorPlacements=%zu entrance=%s. "
                    "Original Player/Camera/Actor lifecycle NOT linked.",
                    assets->descriptor().level, assets->descriptor().overlayId,
                    assets->descriptor().modelHandle, stageModel_.dlists.size(),
                    colliderReady ? "ready" : "unavailable", stageCollision_.prism_count(),
                    stageActors_.actors().size(), stagePilotReady_ ? "found" : "missing");
                sm64ds::diagnostics::note("STAGE", message);
#endif
                ++ticks_; if (!batchFrame_) scanout(); return;
            }
            throw std::runtime_error("Scena originale non collegata: " +
                                     std::to_string(data_02092664));
        }
    }
    boot_.Behavior();
    data_0209f5bc->AdvanceFade();
    ++ticks_;
    if (!batchFrame_) scanout();
}
void GameProgram::scanout() {
#if defined(__ANDROID__)
    // This is a real hardware-state trace from the phone, not a simulated
    // scene. It distinguishes an incorrect sub-BG mode/matrix from lost
    // VRAM bank routing if the title still shows vertical stripes.
    if (activeScene_ == 1 && ticks_ % 120 == 0) {
        const auto io = [](uint32_t address, unsigned width) {
            return static_cast<uint32_t>(ntr::io_read(address, width));
        };
        __android_log_print(ANDROID_LOG_INFO, "SM64DS-PPU",
            "tick=%llu lcd=%04x SUB dispcnt=%08x BG0=%04x BG1=%04x BG2=%04x BG3=%04x "
            "BGP A=%04x B=%04x C=%04x D=%04x X=%08x Y=%08x "
            "VRAM A=%02x B=%02x C=%02x D=%02x H=%02x I=%02x",
            static_cast<unsigned long long>(ticks_),
            io(0x04000304u, 2), io(0x04001000u, 4),
            io(0x04001008u, 2), io(0x0400100au, 2),
            io(0x0400100cu, 2), io(0x0400100eu, 2),
            io(0x04001030u, 2), io(0x04001032u, 2),
            io(0x04001034u, 2), io(0x04001036u, 2),
            io(0x04001038u, 4), io(0x0400103cu, 4),
            io(0x04000240u, 1), io(0x04000241u, 1),
            io(0x04000242u, 1), io(0x04000243u, 1),
            io(0x04000248u, 1), io(0x04000249u, 1));
        char snapshot[230];
        std::snprintf(snapshot, sizeof snapshot,
            "tick=%llu subDISPCNT=%08x BG0=%04x BG1=%04x BG2=%04x BG3=%04x "
            "VRAM_A=%02x B=%02x C=%02x D=%02x H=%02x I=%02x",
            static_cast<unsigned long long>(ticks_), io(0x04001000u, 4),
            io(0x04001008u, 2), io(0x0400100au, 2),
            io(0x0400100cu, 2), io(0x0400100eu, 2),
            io(0x04000240u, 1), io(0x04000241u, 1),
            io(0x04000242u, 1), io(0x04000243u, 1),
            io(0x04000248u, 1), io(0x04000249u, 1));
        sm64ds::diagnostics::note("VIDEO", snapshot);
    }
#endif
    if (activeScene_==0) {
    ntr::io_write(0x04000000,
                  (ntr::io_read(0x04000000, 4) & ~0xf00u) | (uint32_t(data_0209d45c & 15) << 8), 4);
    ntr::io_write(0x04001000,
                  (ntr::io_read(0x04001000, 4) & ~0xf00u) | (uint32_t(data_0209d454 & 15) << 8), 4);
    }
    ntr::Framebuffer geometry{};
    if (activeScene_ == 3) {
        // Original cartridge mesh and material display lists rendered by the
        // same DS geometry engine as the title. Until the actual game camera
        // runs, fit the vertices for an explicitly non-gameplay level preview.
        if (!stagePreviewReady_) {
            ntr::gx_begin_frame();
            ntr::bmd_set_animation(nullptr);
            ntr::bmd_submit(stageModel_);
            ntr::gx_fit_geometry_preview();
            ntr::gx_render(stagePreviewFrame_);
            stagePreviewReady_ = true;
        }
        // Until Player/camera/animated actors are integrated the source geometry
        // is static; do not re-decode and rasterize an entire level each tick.
        main_ = stagePreviewFrame_;
        std::fill(&sub_.px[0][0], &sub_.px[0][0] + 256 * 192, 0xff000000u);
        return;
    }
    if (activeScene_==1) ntr::gx_render(geometry);
    ntr::ppu_scanout(ntr::ENGINE_A, main_,activeScene_==1?&geometry:nullptr);
    ntr::ppu_scanout(ntr::ENGINE_B, sub_);
    if (activeScene_==1) {
        ntr::ppu_scanout_obj(ntr::ENGINE_A,main_);
        ntr::ppu_scanout_obj(ntr::ENGINE_B,sub_);
        ntr::ppu_capture(main_,geometry);
    }
    if (!data_0208ee3c) {
        std::fill(&main_.px[0][0],&main_.px[0][0]+256*192,0xff000000u);
        std::fill(&sub_.px[0][0],&sub_.px[0][0]+256*192,0xff000000u);
    }
}
}

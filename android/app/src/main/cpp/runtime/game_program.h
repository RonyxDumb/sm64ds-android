#pragma once
#include "cartridge.h"
#include "frontend_core.h"
#include "game_input.h"
#include "minigame_catalog.h"
#include "minigame_art.h"
#include "minigame_menu_session.h"
#include "minigame_runtime.h"
#include "minigame_gameplay_runtime.h"
#include "stage_flow.h"
#include "title_files.h"
#include "runtime/boot_state.h"
#include "ntr/ppu.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>
namespace sm64ds {
// Native owner of recovered boot, title-file and stage-flow state. Requests come from recovered
// dScene_c methods; an unlinked destination is reported explicitly.
class GameProgram {
public:
    using Sound = std::function<bool(uint32_t, uint32_t)>;
    GameProgram(const char *path, Sound sound, FrontendCore::Audio audio = {});
    ~GameProgram();
    GameProgram(const GameProgram &) = delete;
    GameProgram &operator=(const GameProgram &) = delete;
    void frame(double seconds, uint32_t buttons, float x, float y, int tx, int ty);
    void tick(uint32_t buttons = 0, float x = 0, float y = 0, int tx = -1, int ty = -1);
    const ntr::Framebuffer &main_screen() const {
        return main_;
    }
    const ntr::Framebuffer &sub_screen() const {
        return sub_;
    }
    uint64_t ticks() const {
        return ticks_;
    }
    uint16_t requested_scene() const;
    // Original 36-row menu data and overlay 5/6 preflight; not a game scene actor.
    const MinigameCatalog &minigames();
    // Original menu record initialization; separate from catalog preflight.
    // Never called by the File Select unless scene 5 actually starts.
    bool prepare_minigame_menu();
    const MinigameMenuSession *minigame_menu_session() const { return minigameMenu_.get(); }
    const MinigameMenuArtwork *minigame_artwork() const { return minigameArtwork_.get(); }
    const MinigameRuntime *minigame_runtime() const { return minigameRuntime_.get(); }
    const MinigameGameplayRuntime *minigame_gameplay_runtime() const { return minigameGameplayRuntime_.get(); }
    int active_scene() const { return activeScene_; }
    const BootState &boot() const {
        return boot_;
    }
    const TitleFiles *title_files() const {
        return title_.get();
    }
    const FrontendCore *frontend_core() const { return frontend_.get(); }
    StageRequest stage_request() const {
        return stage_.request();
    }
    // Original LVL_Overlay and cartridge-backed model/KCL, held across scene
    // activation for the native dScStage_c actor loader.
    const StageAssets *stage_assets() const { return stage_.assets(); }
    std::vector<StageAssets::PlayerSpawnCommand> stage_player_spawn_commands() const;
    packed::Space *stage_address_space() const { return stage_.address_space(); }
    // Receives original frontend results: files 3..5 or minigame room 7.
    bool accept_title_result(int result);
    uint32_t spawn_scene(uint32_t sceneID, uint32_t parameter, uint8_t kind);
    void *load(uint32_t handle);
    void release(void *pointer);
    size_t bytes_at(const void *pointer) const;
    // An owned, exact-size destination for the original LoadFont(2) cartridge bytes.
    // Never aliases the displayed BG/OBJ VRAM banks.
    void *minigame_font_work_buffer();
    void *font_work_destination(void *target, size_t bytes) const;
    bool sound(uint32_t archive, uint32_t entry) {
        return sound_(archive, entry);
    }
    int native_audio(const SoundRequest &request);
    int defer_music(int music,unsigned frames);
    void prepare_boot();
    void queue(void (*callback)(int), int argument);
    bool worker_complete() const {
        return complete_;
    }
    static GameProgram &active();

private:
    Cartridge cartridge_;
    StageFlow stage_;
    std::unique_ptr<TitleFiles> title_;
    std::unique_ptr<FrontendCore> frontend_;
    std::unique_ptr<MinigameCatalog> minigames_;
    std::unique_ptr<MinigameMenuSession> minigameMenu_;
    std::unique_ptr<MinigameMenuArtwork> minigameArtwork_;
    std::unique_ptr<MinigameRuntime> minigameRuntime_;
    std::unique_ptr<MinigameGameplayRuntime> minigameGameplayRuntime_;
    unsigned minigameGroup_=4, minigamePage_=0; // first entry reads ROM's group initializer
    BootState boot_{};
    GameInput input_;
    Sound sound_;
    FrontendCore::Audio audio_;
    int activeScene_=0,pendingMusic_=-1;
    uint16_t musicDelay_=0;
    std::map<void *, size_t> files_;
    std::vector<uint8_t> minigameFontBuffer_;
    ntr::Framebuffer main_{}, sub_{};
    void (*job_)(int) = nullptr;
    int argument_ = 0;
    bool complete_ = false;
    uint64_t ticks_ = 0;
    double accumulated_ = 0;
    bool batchFrame_ = false; // ordinary presentation is deferred, captures are not
    unsigned reflectionTraceFrames_ = 0;
    void finish_tick();
    void scanout();
};
}

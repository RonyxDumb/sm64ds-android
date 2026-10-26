#pragma once
#include "cartridge.h"
#include "frontend_core.h"
#include "game_input.h"
#include "stage_flow.h"
#include "title_files.h"
#include "runtime/boot_state.h"
#include "ntr/ppu.h"
#include <functional>
#include <map>
#include <memory>
namespace sm64ds {
// Native owner of recovered boot, title-file and stage-flow state. Requests come from recovered
// dScene_c methods; an unlinked destination is reported explicitly.
class GameProgram {
public:
    using Sound = std::function<bool(uint32_t, uint32_t)>;
    GameProgram(const char *path, Sound sound);
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
    // Receives the real frontend's return code (3..5), never raw UI coordinates.
    // The title dispatcher and stage actor scene still need native link closure.
    bool accept_title_result(int result);
    void *load(uint32_t handle);
    void release(void *pointer);
    size_t bytes_at(const void *pointer) const;
    bool sound(uint32_t archive, uint32_t entry) {
        return sound_(archive, entry);
    }
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
    BootState boot_{};
    GameInput input_;
    Sound sound_;
    std::map<void *, size_t> files_;
    ntr::Framebuffer main_{}, sub_{};
    void (*job_)(int) = nullptr;
    int argument_ = 0;
    bool complete_ = false;
    uint64_t ticks_ = 0;
    double accumulated_ = 0;
    void scanout();
};
}

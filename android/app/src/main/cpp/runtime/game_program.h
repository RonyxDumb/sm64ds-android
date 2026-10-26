#pragma once
#include "cartridge.h"
#include "game_input.h"
#include "runtime/boot_state.h"
#include "ntr/ppu.h"
#include <functional>
#include <map>
namespace sm64ds {
// Native owner of recovered boot state. Scene requests come from recovered
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
    const ntr::Framebuffer &main_screen() const { return main_; }
    const ntr::Framebuffer &sub_screen() const { return sub_; }
    uint64_t ticks() const { return ticks_; }
    uint16_t requested_scene() const;
    const BootState &boot() const { return boot_; }
    void *load(uint32_t handle);
    void release(void *pointer);
    size_t bytes_at(const void *pointer) const;
    bool sound(uint32_t archive, uint32_t entry) { return sound_(archive, entry); }
    void prepare_boot();
    void queue(void (*callback)(int), int argument);
    bool worker_complete() const { return complete_; }
    static GameProgram &active();
private:
    Cartridge cartridge_;
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

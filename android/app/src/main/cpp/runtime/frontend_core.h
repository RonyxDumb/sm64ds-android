#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include "sound_request.h"
#include <array>
#include <functional>

namespace sm64ds {
struct TitleConfig;
// Owns the original frontend root and complete title initialization. Interactive
// dispatcher/render entry points share this checked native address space.
class FrontendCore {
public:
    using FileLoader = std::function<std::vector<uint8_t>(uint16_t)>;
    using Command = std::function<uint8_t(int)>;
    using Audio = std::function<int(const SoundRequest &)>;
    FrontendCore(GameHeap &heap, Cartridge::DataImage data, int ownerLanguage, FileLoader loader = {},
                 Cartridge::DataImage arm9 = {}, const TitleConfig *config = nullptr,
                 Command command = {}, Audio audio = {});
    ~FrontendCore();
    void sample(uint16_t keys, int x, int y);
    void step_state(unsigned index);
    int update(uint16_t keys, int x, int y);
    void render();
    uint8_t command(int value);
    int start_sound(uint32_t handle, unsigned archive, int entry);
    int control_sound(uint32_t handle, SoundOp op, int a=0, int b=0, int c=0);
    void sound_priority(uint32_t handle, uint8_t priority);
    void detach_sound(uint32_t handle);
    uint32_t root_address() const { return root_; }
    const void *data_at(uint32_t address, size_t bytes) const { return space_.data(address,bytes); }
    uint32_t word_at(uint32_t address) const;
    size_t allocations() const { return space_.allocations(); }
    int language() const { return ownerLanguage_; }
    bool has_items() const { return bool(loader_); }
    uint32_t load_file(uint16_t handle);
    void latch_touch() { latchedTouch_=touch_; }
    const std::array<uint16_t,4> &latched_touch() const { return latchedTouch_; }
    static FrontendCore &current();
    class Scope {
        packed::Space::Scope address_;
        FrontendCore *previous_;
    public:
        explicit Scope(FrontendCore &core);
        ~Scope();
    };
private:
    packed::Space space_;
    Cartridge::DataImage image_;
    Cartridge::DataImage arm9_;
    uint32_t root_=0;
    int ownerLanguage_;
    FileLoader loader_;
    Command command_;
    Audio audio_;
    const TitleConfig *config_=nullptr;
    uint32_t configAddress_=0, nextVoice_=1;
    struct Voice { int player; uint32_t handle; uint8_t priority; bool stopping; };
    std::map<uint32_t,Voice> voices_;
    void retire_sound(uint32_t token);
    void update_sounds();
    std::array<uint16_t,4> touch_{}, latchedTouch_{};
};
}

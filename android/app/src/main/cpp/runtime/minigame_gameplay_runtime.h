#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include <cstdint>
#include <map>
#include <vector>
namespace sm64ds {
// Isolated original overlay-6 address space, replacing overlay 5 at its
// cartridge load address. This is an actor bootstrap, not a fake game loop.
class MinigameGameplayRuntime {
public:
    MinigameGameplayRuntime(GameHeap &, Cartridge &, uint32_t scene, uint32_t parameter,
                            const uint8_t *selectedRow = nullptr);
    ~MinigameGameplayRuntime();
    uint32_t spawn(uint8_t kind);
    void update();
    void cleanup();
    uint32_t actor() const noexcept { return actor_; }
    uint32_t load(uint32_t handle);
    uint32_t load_path(uint32_t path);
    void free(uint32_t address);
    void register_global(uint32_t object,uint32_t destructor,uint32_t node);
    void invoke_irq(bool tableFlip);
    static MinigameGameplayRuntime &current();
    static bool active();
    uint32_t scene() const noexcept { return scene_; }
    uint32_t parameter() const noexcept { return parameter_; }
    const packed::Space &space() const noexcept { return space_; }
private:
    packed::Space space_;
    Cartridge &cartridge_;
    Cartridge::DataImage arm9_, common_, game_;
    uint32_t scene_, parameter_, actor_=0;
    bool cleaned_=false;
    std::vector<std::pair<uint32_t,uint32_t>> destructors_;
    class Scope {
        packed::Space::Scope address_;
        MinigameGameplayRuntime *previous_;
    public:
        explicit Scope(MinigameGameplayRuntime &);
        ~Scope();
    };
};
}

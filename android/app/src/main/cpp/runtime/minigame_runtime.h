#pragma once
#include "cartridge.h"
#include "packed_memory.h"
#include <map>

namespace sm64ds {
class GameProgram;
// Runs the recovered overlay-5 menu on exact 32-bit POD records. The source
// pointer projection preserves the original logic, tables and OAM rendering.
class MinigameRuntime {
public:
    // Group 4 selects the original ARM9 initializer (Yoshi, value 3 in EU).
    MinigameRuntime(Cartridge &, GameProgram &, unsigned group=4, unsigned page=0);
    ~MinigameRuntime();
    void update();
    void render();
    void cleanup();
    unsigned group() const;
    unsigned page() const;
    int scroll() const;
    uint32_t actor() const { return actor_; }
    const packed::Space &space() const { return space_; }
    uint32_t load(int handle);
    void free(uint32_t);
    void reset_files();
    void preload(int archive);
    int language() const;
    GameProgram &program() { return program_; }
    static MinigameRuntime &current();
private:
    packed::Space space_;
    Cartridge &cartridge_;
    GameProgram &program_;
    Cartridge::DataImage arm9_, menu_;
    uint32_t actor_=0;
    std::map<uint32_t, size_t> files_;
    bool cleaned_=false;
    class Scope {
        packed::Space::Scope address_;
        MinigameRuntime *previous_;
    public:
        explicit Scope(MinigameRuntime &);
        ~Scope();
    };
};
}

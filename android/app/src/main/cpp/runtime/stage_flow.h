#pragma once
#include "cartridge.h"
#include "stage_assets.h"
#include "packed_memory.h"
#include "actor_dispatch.h"
#include "runtime/controller_input.h"
#include "types.h"
#include <memory>

// The pointer-free controller record shared by the stage and Player. Unlike a
// DS actor, this record has the same size and offsets in a native ARM64 process.
namespace sm64ds {
struct StageRequest {
    int level, entrance, star, character, players, controllerMode;
};

class StageFlow {
public:
    explicit StageFlow(Cartridge &cartridge);
    StageRequest request() const;
    const ControllerInput &controller(unsigned slot) const;
    const StageAssets *assets() const { return assets_.get(); }
    packed::Space *address_space() const { return addressSpace_.get(); }
    void prepare_scene();
    uint32_t spawn_scene(uint32_t sceneID, uint32_t parameter, uint8_t kind);
    void update_scene();
    std::vector<StageAssets::PlayerSpawnCommand> player_spawn_commands(uint8_t savedCharacter) const;
    void prepare_input();
    void update_input(u32 cameraFlags, float stickX = 0, float stickY = 0);
    // Execute the recovered fBase_c::Process callback ABI using registered
    // ARM64 native code, never a guest ARM function pointer.
    int process_actor(uint32_t actor, const ActorProcessMethods &methods);
    uint32_t process_actor_list(uint32_t descriptor);
    int invoke_player_state(uint32_t player, uint32_t state, PlayerStatePhase phase);

private:
    Cartridge &cartridge_;
    std::unique_ptr<StageAssets> assets_;
    // Original ARM9 + level overlay data, exposed at unchanged DS addresses
    // through checked native handles (not raw ARM64 pointer narrowing).
    std::unique_ptr<Cartridge::DataImage> arm9Image_;
    std::unique_ptr<Cartridge::DataImage> commonOverlay_;
    std::unique_ptr<packed::Space> addressSpace_;
    void reset_input();
    void check_camera_input(u32 cameraFlags);
    void check_input();
    void sync_state(bool toPacked);
    bool inputEnabled_ = false;
    uint32_t sceneActor_ = 0;
    bool sceneInitialized_ = false;
};
}

extern "C" {
extern int data_0209caa0[];
extern s8 SUBLEVEL_LEVEL_TABLE[0x34];
extern s8 data_02092110, data_02092118, data_0209211c, data_0209f2f8;
extern s8 data_02092124, data_0209f2f4;
extern u8 data_02092114, data_02092128[4], data_0208a0e0;
extern s16 data_02092144[4];
extern u8 data_0209f1f0, data_0209f220, data_0209f26c, data_0209f200;
extern u8 data_0209f268, data_0209f24c, data_0209f270, data_0209f2d8, data_0209f254;
extern u8 data_0209f228[4], data_0209f4ae[];
extern sm64ds::ControllerInput data_0209f498[4];
extern u8 data_0209f21c, data_0209f350[4], data_0209fc48;
extern u8 data_0209d660;
extern u8 data_0209f294, data_0209f2c4, data_0209f20c, data_020a1166[4 * 0x24];
extern u16 data_0209f368[4];
extern s16 data_020756b0[16], data_02082214[8192];
extern u16 *data_0209214c[3], data_020994e0[1025];
extern u8 data_ov002_02111180;
extern u8 data_0209fc50, data_0209fc5c[4];
extern char data_0209fc64[4];
int SublevelToLevel(int level);
void SetNumPlayers(int count);
void InitControllerMode(int slot, u8 mode);
void SetControllerMode(u8 mode);
void SetNextStar();
void SetPlayerGlobals();
void LoadLevel(int level, u32 entrance, int star, u32 flags, s8 returnState);
void LoadLevelNoReturn(int level, u32 entrance, u32 star, u32 flags);
void StartFile(s8 level, u8 entrance);
u8 DecIfAbove0_Byte(u8 *value);
u8 GetControllerMode(int slot);
void func_0205a588(void *target, int value, int size);
}

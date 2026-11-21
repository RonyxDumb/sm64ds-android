// How the host side (Android activity, renderer, audio) drives the game. Plain calls in the single-image builds
// (hostside/direct.c); calls into the guest image on a stack below 4 GiB in the arm64 build (hostside/bridge.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "port.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

bool guest_load(void);                        // arm64: map the guest image; false when that is impossible
void guest_start_game(void);                  // starts the game thread (returns at once)
const FrameSnapshot *guest_frame_acquire(void);
void guest_snd_render(int16_t *out, int frames, int rate);
void guest_input_set(const PortInput *in);
void guest_save_flush(void);
void guest_set_paused(bool paused);
void guest_set_vblank_rate(uint32_t mhz);
void guest_set_aspect(int32_t fx12); // race camera aspect ratio (20.12), from the next race
void guest_set_language(int language); // 1 English, 2 French, 3 German, 4 Italian, 5 Spanish; before the start
// The player's name for multiplayer and races: PORT_PLAYER_NAME UTF-16 units, 10 characters and zeros; empty for
// the save file's nickname. At any time; used from the next multiplayer session or race.
#define PORT_PLAYER_NAME 12
void guest_set_player_name(const uint16_t *name);
uint64_t guest_frame_count(void);
// Tests: the game stops after each frame until `done` is posted; `ready` is posted when a frame is captured.
void guest_set_lockstep(void *ready, void *done);

#ifdef __cplusplus
}
#endif

// Guest image entry: the header the host reads at GUEST_BASE, and the import table it fills.
#include "port.h"
#include "frame.h"
#include "guest_abi.h"

void GameMain(void);
void port_overlays_init(void);
void port_snd_render(int16_t *out, int frames, int rate);

#define COUNT(name) +1
uint64_t g_host_imports[0 HOST_IMPORTS(COUNT)] __attribute__((used));

static void guest_start(void) {
    port_overlays_init();
    gx3d_init();
    port_os_run(GameMain);
}

extern int g_port_aspect, g_port_language;
extern uint16_t g_port_player_name[12];
__attribute__((section(".guest_header"), used)) const GuestHeader g_guest_header = {
    .magic = GUEST_MAGIC,
    .imports = (uint32_t) (uintptr_t) g_host_imports,
    .import_count = 0 HOST_IMPORTS(COUNT),
    .start = (uint32_t) (uintptr_t) guest_start,
    .frame_acquire = (uint32_t) (uintptr_t) port_frame_acquire,
    .snd_render = (uint32_t) (uintptr_t) port_snd_render,
    .input_set = (uint32_t) (uintptr_t) port_input_set,
    .save_flush = (uint32_t) (uintptr_t) port_save_flush,
    .paused = (uint32_t) (uintptr_t) &g_paused,
    .quit = (uint32_t) (uintptr_t) &g_quit,
    .vblank_rate = (uint32_t) (uintptr_t) &g_vblank_rate_mhz,
    .aspect = (uint32_t) (uintptr_t) &g_port_aspect,
    .language = (uint32_t) (uintptr_t) &g_port_language,
    .player_name = (uint32_t) (uintptr_t) g_port_player_name,
    .frame_count = (uint32_t) (uintptr_t) &g_frame_count,
    .lockstep_ready = (uint32_t) (uintptr_t) &g_lockstep_ready,
    .lockstep_done = (uint32_t) (uintptr_t) &g_lockstep_done,
};

// The host functions themselves are jumps through the import table: tools/build_guest.py generates them
// (imports.S) from HOST_IMPORTS.

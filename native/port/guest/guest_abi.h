// The interface between the 64-bit host library and the guest image (32-bit pointers, below 4 GiB).
// Shared by both sides: only fixed-size types.
#pragma once
#include <stdint.h>

#define GUEST_BASE 0x10000000u
#define GUEST_MAGIC 0x47444b4du // "MKDG"

// Host functions the guest calls, in this order (the guest's import table holds their 64-bit addresses).
#define HOST_IMPORTS(X)                                                                                                \
    X(hapi_log) X(hapi_abort) X(hapi_now_ns) X(hapi_sleep_ns) X(hapi_local_time) X(hapi_mutex_new) X(hapi_mutex_lock)  \
    X(hapi_mutex_unlock) X(hapi_sem_new) X(hapi_sem_free) X(hapi_sem_post) X(hapi_sem_wait) X(hapi_thread_start)       \
    X(hapi_thread_exit) X(hapi_tls_get) X(hapi_tls_set) X(hapi_vram_banks) X(hapi_vram_remap) X(hapi_rom)            \
    X(hapi_save_read) X(hapi_save_write) X(hapi_net_open) X(hapi_net_close) X(hapi_net_mac) X(hapi_net_send) X(hapi_net_recv)

// At GUEST_BASE: what the host needs to start and drive the guest. Addresses are guest (32-bit) addresses.
typedef struct GuestHeader {
    uint32_t magic;
    uint32_t imports;       // uint64_t[import count]
    uint32_t import_count;
    uint32_t start;         // void (void): runs the game, does not return
    uint32_t frame_acquire; // const FrameSnapshot *(void)
    uint32_t snd_render;    // void (int16_t *out, int frames, int rate)
    uint32_t input_set;     // void (const PortInput *)
    uint32_t save_flush;    // void (void)
    uint32_t paused;        // volatile bool *
    uint32_t quit;          // volatile bool *
    uint32_t vblank_rate;   // uint32_t *
    uint32_t frame_count;   // uint64_t *
    uint32_t lockstep_ready; // void ** (tests)
    uint32_t lockstep_done;  // void **
    uint32_t aspect;         // int32_t *: race camera aspect ratio, 20.12 fixed point
    uint32_t language;       // int32_t *: the console language (OS_GetOwnerInfo), set before the start
    uint32_t player_name;    // uint16_t[12] *: the player's name for multiplayer and races (empty: the save file's)
} GuestHeader;

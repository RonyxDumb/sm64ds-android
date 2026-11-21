// The game in its own process (arm64 Android).
//
// A 64-bit app process already has the Java runtime's memory in the low 4 GiB, where the game needs the DS address
// space and its 32-bit-pointer image. So the app starts the game as a separate native process (libmkdsgame.so, an
// executable shipped with the native libraries), whose address space is empty. The two share:
//  - this block: input and commands one way; frames (triple buffer), audio (ring) and status the other way;
//  - the VRAM banks, which the renderer's display capture writes into.
// The app keeps the renderers, the audio output and the Android side; the game process runs the game and its
// DS platform layer, and copies each new frame snapshot and the audio it renders into the block.
#pragma once

#include <stdatomic.h>
#include <stdint.h>

#include "frame.h"
#include "port.h"

#define IPC_MAGIC 0x4d4b4950u
#define IPC_AUDIO_RING 16384u // stereo frames, power of two
#define IPC_FRAME_NEW 4u

enum { IPC_STARTING, IPC_RUNNING, IPC_FAILED };

typedef struct IpcShared {
    uint32_t magic;
    _Atomic uint32_t state;
    char error[1024];

    // app -> game
    _Atomic uint32_t input_seq; // even when `input` is stable
    PortInput input;
    _Atomic uint32_t paused, quit, vblank_rate;
    _Atomic uint32_t flush_req, flush_ack;
    _Atomic uint32_t audio_rate, audio_burst;
    _Atomic uint32_t captures; // display captures done by the renderer
    _Atomic int32_t aspect;    // race camera aspect ratio (20.12), 0 = unchanged
    _Atomic int32_t language;  // console language, read at the start (0 = default)
    _Atomic uint32_t player_name_seq; // counts the changes of player_name
    uint16_t player_name[12];         // the player's name for multiplayer and races

    // game -> app
    _Atomic uint32_t frame_count;
    _Atomic uint32_t frame_slot; // the middle buffer of the triple buffer | IPC_FRAME_NEW
    _Atomic uint32_t audio_write, audio_read;
    int16_t audio[IPC_AUDIO_RING * 2];
    FrameSnapshot frames[3];
} IpcShared;

// The used part of a snapshot (the 3D list arrays are only partly filled).
static inline void ipc_copy_snapshot(FrameSnapshot *dst, const FrameSnapshot *src) {
    __builtin_memcpy(dst, src, offsetof(FrameSnapshot, frame3d));
    const Gx3dFrame *a = &src->frame3d;
    Gx3dFrame *b = &dst->frame3d;
    uint32_t nv = a->vertex_count < GX_MAX_VERTICES ? a->vertex_count : GX_MAX_VERTICES;
    uint32_t np = a->polygon_count < GX_MAX_POLYGONS ? a->polygon_count : GX_MAX_POLYGONS;
    __builtin_memcpy(b->vertices, a->vertices, nv * sizeof(GxVertex));
    __builtin_memcpy(b->polygons, a->polygons, np * sizeof(GxPolygon));
    __builtin_memcpy(&b->vertex_count, &a->vertex_count, sizeof(Gx3dFrame) - offsetof(Gx3dFrame, vertex_count));
    dst->number = src->number;
    dst->scene = src->scene;
    dst->time_ns = src->time_ns;
}

// App side (ipc/client.c): starts the game process. NULL on success, else a message for the user.
const char *ipc_start(const char *exe, const char *rom, const char *save, const char *dir, uint32_t vblank_rate);

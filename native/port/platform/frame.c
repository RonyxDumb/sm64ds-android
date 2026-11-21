// VBlank: what the DS shows during a frame is captured here for the renderer, then the geometry engine swaps its
// lists and the input registers are updated, before the game's VBlank interrupt handler runs (os.c).
#include "port.h"
#include "frame.h"

#include "hostapi.h"
#include <stdatomic.h>
#include <string.h>

void port_tp_vblank(void);
int GetCurrentSceneNumber(void);

// Aspect ratio of the race cameras (20.12): 4:3 on the DS, the screen's for widescreen (tools/portify.py points the
// camera setup here).
int g_port_aspect = 0x1555;

void (*g_frame_hook)(const FrameSnapshot *snapshot);
// Tests: when set, each captured frame posts `ready` and waits for `done` (host semaphores).
void *g_lockstep_ready, *g_lockstep_done;

// Triple buffer: the writer fills `s_write`, publishes it as `s_ready`; the reader swaps `s_ready` with `s_read`.
static FrameSnapshot s_buffers[3];
static int s_write = 0, s_read = 2;
static _Atomic int s_ready = 1;
static atomic_bool s_fresh;
static uint64_t s_number;

static void capture(FrameSnapshot *f) {
    memcpy(f->io, (const void *) (uintptr_t) DS_IO_BASE, 0x70);
    memcpy(f->io + 0x1000, (const void *) (uintptr_t) (DS_IO_BASE + 0x1000), 0x70);
    memcpy(f->io + 0x200, (const void *) (uintptr_t) (DS_IO_BASE + 0x200), 0x60); // VRAMCNT, POWCNT
    memcpy(f->io + 0x300, (const void *) (uintptr_t) (DS_IO_BASE + 0x300), 0x10); // POWCNT1
    memcpy(f->palette, (const void *) (uintptr_t) DS_PLTT_BASE, sizeof(f->palette));
    memcpy(f->oam, (const void *) (uintptr_t) DS_OAM_BASE, sizeof(f->oam));
    memcpy(f->vram, hapi_vram_banks(), sizeof(f->vram));
    memcpy(f->vramcnt, (const void *) (uintptr_t) (DS_IO_BASE + 0x240), sizeof(f->vramcnt));
    const Gx3dFrame *g = gx3d_displayed();
    Gx3dFrame *d = &f->frame3d;
    memcpy(d->vertices, g->vertices, sizeof(GxVertex) * g->vertex_count);
    memcpy(d->polygons, g->polygons, sizeof(GxPolygon) * g->polygon_count);
    size_t header = offsetof(Gx3dFrame, vertex_count);
    memcpy((uint8_t *) d + header, (const uint8_t *) g + header, sizeof(Gx3dFrame) - header);
    f->scene = (uint8_t) GetCurrentSceneNumber();
    f->time_ns = hapi_now_ns();
    f->number = ++s_number;
}

const FrameSnapshot *port_frame_acquire(void) {
    if (!atomic_exchange(&s_fresh, false)) {
        return NULL;
    }
    s_read = atomic_exchange(&s_ready, s_read);
    return &s_buffers[s_read];
}

static PortInput s_input;
static void *s_input_lock;
static void input_lock(void) {
    if (!s_input_lock) s_input_lock = hapi_mutex_new();
    hapi_mutex_lock(s_input_lock);
}

void port_input_set(const PortInput *in) {
    input_lock();
    s_input = *in;
    hapi_mutex_unlock(s_input_lock);
}

void port_input_get(PortInput *out) {
    input_lock();
    *out = s_input;
    hapi_mutex_unlock(s_input_lock);
}

static void update_input_registers(void) {
    PortInput in;
    port_input_get(&in);
    IO16(0x130) = (uint16_t) (~in.buttons & 0x3ff); // KEYINPUT, active low
    // What the ARM7 leaves at 0x027fffa8, in KEYINPUT positions: X (bit 10), Y (bit 11) and debug (bit 13), active
    // low, and the hinge (bit 15, set when closed).
    uint16_t xy = 0x2000;
    if (!(in.buttons & PAD_X)) xy |= 0x0400;
    if (!(in.buttons & PAD_Y)) xy |= 0x0800;
    if (in.lid_closed) xy |= 0x8000;
    *(volatile uint16_t *) (uintptr_t) 0x027fffa8 = xy;
}

void port_frame_vblank(void) {
    FrameSnapshot *f = &s_buffers[s_write];
    capture(f);
    if (g_frame_hook) {
        g_frame_hook(f);
    }
    s_write = atomic_exchange(&s_ready, s_write);
    atomic_store(&s_fresh, true);
    if (g_lockstep_ready) {
        hapi_sem_post(g_lockstep_ready);
        hapi_sem_wait(g_lockstep_done);
    }

    gx3d_vblank();
    update_input_registers();
    port_tp_vblank();
    IO16(0x006) = 192;              // VCOUNT
    IO16(0x004) |= 1;               // DISPSTAT: in VBlank
    if (g_frame_count % 120 == 119) {
        port_save_flush();
    }
}

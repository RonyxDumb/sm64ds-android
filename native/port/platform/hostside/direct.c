// Host side of the single-image builds (32-bit Android, Linux test build): the guest is the same program, so its
// threads are ordinary threads and any memory is addressable by it.
#include "port.h"

#include <pthread.h>
#include <stdlib.h>

void *host_low_alloc(size_t size) { return calloc(1, size); }

typedef struct Start {
    void (*fn)(void *);
    void *arg;
} Start;

static void *trampoline(void *p) {
    Start s = *(Start *) p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

void host_start_guest_thread(void (*fn)(void *), void *arg) {
    Start *s = malloc(sizeof(*s));
    s->fn = fn;
    s->arg = arg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    if (pthread_create(&t, &attr, trampoline, s) != 0) {
        port_fatal("host: pthread_create failed");
    }
    pthread_attr_destroy(&attr);
}

// ------------------------------------------------------------------------------------------------ guest interface
#include "guest_if.h"

void GameMain(void);
void port_overlays_init(void);
void port_snd_render(int16_t *out, int frames, int rate);

bool guest_load(void) { return true; }

static void game_thread(void *arg) {
    (void) arg;
    port_overlays_init();
    gx3d_init();
    port_os_run(GameMain);
}

void guest_start_game(void) { host_start_guest_thread(game_thread, NULL); }
const FrameSnapshot *guest_frame_acquire(void) { return port_frame_acquire(); }
void guest_snd_render(int16_t *out, int frames, int rate) { port_snd_render(out, frames, rate); }
void guest_input_set(const PortInput *in) { port_input_set(in); }
void guest_save_flush(void) { port_save_flush(); }
void guest_set_paused(bool paused) { g_paused = paused; }
void guest_set_vblank_rate(uint32_t mhz) { g_vblank_rate_mhz = mhz; }
extern int g_port_aspect;
void guest_set_aspect(int32_t fx12) { g_port_aspect = fx12; }
extern int g_port_language;
void guest_set_language(int language) { g_port_language = language; }
extern uint16_t g_port_player_name[12];
void guest_set_player_name(const uint16_t *name) {
    __builtin_memcpy(g_port_player_name, name, PORT_PLAYER_NAME * sizeof(uint16_t));
}
uint64_t guest_frame_count(void) { return g_frame_count; }
void guest_set_lockstep(void *ready, void *done) {
    g_lockstep_ready = ready;
    g_lockstep_done = done;
}

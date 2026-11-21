// The game process (see ipc.h). Usage: libmkdsgame.so <block fd> <VRAM fd> <rom> <save> <files dir>
#include "ipc.h"
#include "guest_if.h"
#include "hostapi.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <signal.h>
#endif

void *host_low_alloc(size_t size);
#ifdef __ANDROID__
void android_crash_init(const char *dir, bool append);
#endif

static IpcShared *s;

static void fail(const char *what) {
    snprintf(s->error, sizeof(s->error), "%s", what);
    // What occupies the low addresses, for the report.
    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps) {
        char line[256];
        size_t len = strlen(s->error);
        while (fgets(line, sizeof(line), maps) && len + strlen(line) + 1 < sizeof(s->error)) {
            if (strtoull(line, NULL, 16) >= 0x60000000ull) break;
            memcpy(s->error + len, line, strlen(line) + 1);
            len += strlen(line);
        }
        fclose(maps);
    }
    port_log("game process: %s", s->error);
    atomic_store(&s->state, IPC_FAILED);
    _exit(1);
}

static void sleep_us(long us);

// Audio on its own thread, so that copying frames never delays it: keeps the ring about 40 ms ahead of the output.
static void *audio_thread(void *arg) {
    (void) arg;
    static int16_t chunk[1024 * 2];
    for (;;) {
        uint32_t audio_rate = atomic_load(&s->audio_rate);
        if (audio_rate && !atomic_load(&s->paused)) {
            uint32_t target = audio_rate / 25, burst = atomic_load(&s->audio_burst) * 3;
            if (target < burst) target = burst;
            if (target > IPC_AUDIO_RING / 2) target = IPC_AUDIO_RING / 2;
            for (;;) {
                uint32_t w = atomic_load(&s->audio_write), filled = w - atomic_load(&s->audio_read);
                if (filled >= target) break;
                uint32_t n = target - filled;
                if (n > 1024) n = 1024;
                guest_snd_render(chunk, (int) n, (int) audio_rate);
                for (uint32_t i = 0; i < n; i++) {
                    uint32_t at = (w + i) & (IPC_AUDIO_RING - 1);
                    s->audio[at * 2] = chunk[i * 2];
                    s->audio[at * 2 + 1] = chunk[i * 2 + 1];
                }
                atomic_store(&s->audio_write, w + n);
            }
        }
        sleep_us(2000);
    }
    return NULL;
}

static void sleep_us(long us) {
    struct timespec ts = {0, us * 1000};
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <block fd> <vram fd> <rom> <save> <dir>\n", argv[0]);
        return 2;
    }
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGKILL); // ends with the app
#endif
#ifdef __ANDROID__
    android_crash_init(argv[5], true);
#endif
    int block_fd = atoi(argv[1]), vram_fd = atoi(argv[2]);
    s = mmap(NULL, sizeof(IpcShared), PROT_READ | PROT_WRITE, MAP_SHARED, block_fd, 0);
    if (s == MAP_FAILED || s->magic != IPC_MAGIC) {
        port_log("game process: no shared block");
        return 1;
    }
    if (!port_mem_init_fd(vram_fd)) fail("The game process cannot reserve the DS address space.\n");
    if (!guest_load()) fail("The game process cannot load the game image.\n");
    if (!port_rom_open(argv[3])) fail("The ROM could not be opened.");
    port_save_set_path(argv[4]);

    uint32_t rate = atomic_load(&s->vblank_rate), paused = 0, input_seq = 0, flush = 0, captures = 0;
    int32_t aspect = 0;
    uint32_t name_seq = 0;
    guest_set_vblank_rate(rate);
    if (atomic_load(&s->language)) guest_set_language(atomic_load(&s->language));
    if (atomic_load(&s->aspect)) guest_set_aspect(atomic_load(&s->aspect));
    PortInput *in = host_low_alloc(sizeof(*in));
    guest_start_game();
    atomic_store(&s->state, IPC_RUNNING);
    pthread_t audio;
    pthread_create(&audio, NULL, audio_thread, NULL);
    port_log("game process running");

    uint32_t back = 2; // the app reads slot 0 first, slot 1 is the middle one
    atomic_store(&s->frame_slot, 1);
    for (;;) {
        if (atomic_load(&s->quit)) {
            guest_save_flush();
            _exit(0);
        }
        uint32_t seq = atomic_load(&s->input_seq);
        if (seq != input_seq && !(seq & 1)) {
            PortInput copy = s->input;
            atomic_thread_fence(memory_order_acquire);
            if (atomic_load(&s->input_seq) == seq) {
                *in = copy;
                guest_input_set(in);
                input_seq = seq;
            }
        }
        uint32_t p = atomic_load(&s->paused);
        if (p != paused) guest_set_paused(paused = p);
        uint32_t r = atomic_load(&s->vblank_rate);
        if (r != rate) guest_set_vblank_rate(rate = r);
        uint32_t req = atomic_load(&s->flush_req);
        if (req != flush) {
            guest_save_flush();
            atomic_store(&s->flush_ack, flush = req);
        }

        uint32_t ns = atomic_load(&s->player_name_seq);
        if (ns != name_seq) {
            name_seq = ns;
            uint16_t name[PORT_PLAYER_NAME];
            memcpy(name, s->player_name, sizeof(name));
            guest_set_player_name(name);
        }
        int32_t a = atomic_load(&s->aspect);
        if (a && a != aspect) guest_set_aspect(aspect = a);
        uint32_t c = atomic_load(&s->captures);
        if (c != captures) {
            captures = c;
            *(volatile uint8_t *) (uintptr_t) 0x04000067 &= 0x7f; // DISPCAPCNT: capture finished
        }

        const FrameSnapshot *f = guest_frame_acquire();
        if (f) {
            ipc_copy_snapshot(&s->frames[back], f);
            back = atomic_exchange(&s->frame_slot, back | IPC_FRAME_NEW) & 3;
        }
        atomic_store(&s->frame_count, (uint32_t) guest_frame_count());

        sleep_us(1000);
    }
}

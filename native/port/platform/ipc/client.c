// App side of the game process (see ipc.h): the guest interface (guest_if.h) over the shared block.
#include "ipc.h"
#include "guest_if.h"
#include "gpu2d.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
void android_log_line(const char *line);
#endif

static IpcShared *s;
static int s_block_fd = -1, s_vram_fd = -1;
static volatile pid_t s_pid;
static volatile bool s_exited;
static uint32_t s_front;

// ------------------------------------------------------------------------------------------------ logging
void port_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_INFO, "mkds", line);
    android_log_line(line);
#else
    fprintf(stderr, "%s\n", line);
#endif
}

void port_fatal(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    port_log("FATAL: %s", line);
    abort();
}

void *host_low_alloc(size_t size) { return calloc(1, size); }

static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}

static void capture_done(void) { atomic_fetch_add(&s->captures, 1); }

// ------------------------------------------------------------------------------------------------ start
bool guest_load(void) {
    s_block_fd = port_shared_create("mkds-ipc", sizeof(IpcShared));
    s_vram_fd = port_shared_create("ds-vram", VRAM_TOTAL_SIZE);
    if (s_block_fd < 0 || s_vram_fd < 0) {
        port_log("ipc: cannot create the shared memory");
        return false;
    }
    s = mmap(NULL, sizeof(IpcShared), PROT_READ | PROT_WRITE, MAP_SHARED, s_block_fd, 0);
    g_vram_banks = mmap(NULL, VRAM_TOTAL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, s_vram_fd, 0);
    if (s == MAP_FAILED || g_vram_banks == MAP_FAILED) {
        port_log("ipc: cannot map the shared memory");
        return false;
    }
    s->magic = IPC_MAGIC;
    g_capture_done = capture_done;
    atomic_store(&s->frame_slot, 1);
    s_front = 0;
    return true;
}

static void *watch(void *arg) {
    (void) arg;
    int status = 0;
    while (waitpid(s_pid, &status, 0) < 0 && errno == EINTR) {
    }
    s_exited = true;
    if (WIFSIGNALED(status)) {
        port_log("ipc: the game process ended with signal %d", WTERMSIG(status));
    } else {
        port_log("ipc: the game process exited (%d)", WEXITSTATUS(status));
    }
    if (atomic_load(&s->state) == IPC_RUNNING) {
        // It crashed while playing: its crash handler wrote the report; the app shows it at the next start.
        _exit(1);
    }
    return NULL;
}

const char *ipc_start(const char *exe, const char *rom, const char *save, const char *dir, uint32_t vblank_rate) {
    static char err[1200];
    if (!s) return "The shared memory for the game could not be created.";
    if (access(exe, X_OK) != 0) {
        snprintf(err, sizeof(err), "The game program is missing or not executable (%s: %s).", exe, strerror(errno));
        return err;
    }
    atomic_store(&s->vblank_rate, vblank_rate);
    atomic_store(&s->state, IPC_STARTING);
    char block_arg[16], vram_arg[16];
    snprintf(block_arg, sizeof(block_arg), "%d", s_block_fd);
    snprintf(vram_arg, sizeof(vram_arg), "%d", s_vram_fd);
    char *argv[] = {(char *) exe, block_arg, vram_arg, (char *) rom, (char *) save, (char *) dir, NULL};
    extern char **environ;
    pid_t pid = fork();
    if (pid == 0) {
        // Only async-signal-safe calls between fork and exec.
        fcntl(s_block_fd, F_SETFD, 0);
        fcntl(s_vram_fd, F_SETFD, 0);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        execve(exe, argv, environ);
        _exit(127);
    }
    if (pid < 0) {
        snprintf(err, sizeof(err), "The game process could not be started (%s).", strerror(errno));
        return err;
    }
    s_pid = pid;
    pthread_t t;
    pthread_create(&t, NULL, watch, NULL);
    pthread_detach(t);
    for (int i = 0; i < 2000 && atomic_load(&s->state) == IPC_STARTING && !s_exited; i++) {
        sleep_us(5000);
    }
    uint32_t state = atomic_load(&s->state);
    if (state == IPC_RUNNING) return NULL;
    if (state == IPC_FAILED) {
        snprintf(err, sizeof(err), "%s", s->error);
    } else if (s_exited) {
        snprintf(err, sizeof(err), "The game process ended while starting (see log.txt).");
    } else {
        snprintf(err, sizeof(err), "The game process did not start in time.");
    }
    return err;
}

// ------------------------------------------------------------------------------------------------ interface
void guest_start_game(void) {}

const FrameSnapshot *guest_frame_acquire(void) {
    if (!s || !(atomic_load(&s->frame_slot) & IPC_FRAME_NEW)) return NULL;
    s_front = atomic_exchange(&s->frame_slot, s_front) & 3;
    return &s->frames[s_front];
}

void guest_snd_render(int16_t *out, int frames, int rate) {
    if (!s) {
        memset(out, 0, (size_t) frames * 4);
        return;
    }
    atomic_store(&s->audio_rate, (uint32_t) rate);
    if (atomic_load(&s->audio_burst) < (uint32_t) frames) atomic_store(&s->audio_burst, (uint32_t) frames);
    uint32_t r = atomic_load(&s->audio_read), avail = atomic_load(&s->audio_write) - r;
    uint32_t n = avail < (uint32_t) frames ? avail : (uint32_t) frames;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = (r + i) & (IPC_AUDIO_RING - 1);
        out[i * 2] = s->audio[at * 2];
        out[i * 2 + 1] = s->audio[at * 2 + 1];
    }
    memset(out + n * 2, 0, (size_t) (frames - (int) n) * 4); // underrun: silence
    atomic_store(&s->audio_read, r + n);
}

void guest_input_set(const PortInput *in) {
    if (!s) return;
    uint32_t seq = atomic_load(&s->input_seq);
    atomic_store(&s->input_seq, seq + 1);
    atomic_thread_fence(memory_order_release);
    s->input = *in;
    atomic_store(&s->input_seq, seq + 2);
}

void guest_save_flush(void) {
    if (!s || !s_pid || s_exited) return;
    uint32_t req = atomic_load(&s->flush_req) + 1;
    atomic_store(&s->flush_req, req);
    for (int i = 0; i < 1000 && atomic_load(&s->flush_ack) != req && !s_exited; i++) {
        sleep_us(1000);
    }
}

void guest_set_paused(bool paused) {
    if (s) atomic_store(&s->paused, paused);
}
void guest_set_language(int language) {
    if (s) atomic_store(&s->language, language);
}
void guest_set_player_name(const uint16_t *name) {
    if (!s) return;
    memcpy(s->player_name, name, sizeof(s->player_name));
    atomic_fetch_add(&s->player_name_seq, 1);
}
void guest_set_aspect(int32_t fx12) {
    if (s) atomic_store(&s->aspect, fx12);
}
void guest_set_vblank_rate(uint32_t mhz) {
    if (s) atomic_store(&s->vblank_rate, mhz);
}
uint64_t guest_frame_count(void) { return s ? atomic_load(&s->frame_count) : 0; }
void guest_set_lockstep(void *ready, void *done) { (void) ready, (void) done; }

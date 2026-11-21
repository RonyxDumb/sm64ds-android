// Headless test runner (32-bit Linux): runs the game as fast as possible, feeds scripted input and writes frames as
// images. Usage: mkds <rom.nds> [--frames N] [--dump-every K] [--out DIR] [--press FRAME:BUTTONS[:LEN]]...
// BUTTONS is a string of A B s(elect) S(tart) R L u d l r X Y, or "t<x>,<y>" for a touch.
#include "port.h"
#include "frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void host_write_frame(const FrameSnapshot *f, const char *path); // host/render.c

typedef struct Press {
    uint64_t frame, length;
    uint16_t buttons;
    bool touch;
    uint16_t x, y;
} Press;

static Press s_presses[256];
static int s_press_count;
static uint64_t s_frames = 600, s_dump_every = 60;
static const char *s_out = "frames";

#include "guest_if.h"
#ifdef PORT_IPC
#include "ipc/ipc.h"
#include <time.h>
#endif
#include "hostapi.h"

void *host_low_alloc(size_t size);
static FILE *s_wav;
static uint32_t s_wav_frames;
static double s_audio_accum;

static void wav_header(FILE *f, uint32_t frames) {
    uint32_t data = frames * 4, rate = 48000, byte_rate = rate * 4;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    uint32_t riff = 36 + data;
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt_len = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fseek(f, 0, SEEK_END);
}

// One captured frame: scripted input for the next one, audio, image.
static bool s_bglog;
static int32_t s_wide_fx;
extern int g_gx_log;
static bool on_frame(const FrameSnapshot *f, uint64_t n) {
    if (getenv("SM64DS_GXLOG") && n == (uint64_t) atoi(getenv("SM64DS_GXLOG"))) g_gx_log = 400;
    if (s_bglog && getenv("MKDS_OAM") && n == (uint64_t) atoi(getenv("MKDS_OAM"))) {
        const uint16_t *oam = (const uint16_t *) f->oam;
        for (int i = 0; i < 128; i++) {
            uint16_t a0 = oam[i * 4], a1 = oam[i * 4 + 1];
            if (!(a0 & 0x100) && (a0 & 0x200)) continue;
            port_log("oam %d y %d x %d shape %d size %d affine %d dbl %d mode %d", i, a0 & 0xff, a1 & 0x1ff, a0 >> 14,
                     a1 >> 14, (a0 >> 8) & 1, (a0 >> 9) & 1, (a0 >> 10) & 3);
        }
    }
    if (getenv("MKDS_SUBLOG") && n >= (uint64_t) atoi(getenv("MKDS_SUBLOG")) && n < (uint64_t) atoi(getenv("MKDS_SUBLOG")) + 6) { // tests: the sub engine's state
        port_log("frame %llu capcnt %08x POWCNT %04x DISPCNT A %08x", (unsigned long long) n, *(const uint32_t *) (f->io + 0x64),
                 *(const uint16_t *) (f->io + 0x304), *(const uint32_t *) f->io);
        const uint16_t *b = (const uint16_t *) (f->io + 0x1000);
        port_log("sub DISPCNT %04x%04x BGCNT %04x %04x %04x %04x VRAMCNT %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                 b[1], b[0], b[4], b[5], b[6], b[7], f->vramcnt[0], f->vramcnt[1], f->vramcnt[2], f->vramcnt[3],
                 f->vramcnt[4], f->vramcnt[5], f->vramcnt[6], f->vramcnt[8], f->vramcnt[9]);
        port_log("sub BG2 PA %04x PB %04x PC %04x PD %04x X %08x Y %08x BG3 PA %04x X %08x", b[0x10], b[0x11], b[0x12],
                 b[0x13], *(const uint32_t *) (b + 0x14), *(const uint32_t *) (b + 0x16), b[0x18],
                 *(const uint32_t *) (b + 0x1c));
    }
    if (s_bglog) {
        const uint16_t *io = (const uint16_t *) f->io;
        port_log("win %04x %04x %04x %04x in %04x out %04x", io[0x20], io[0x21], io[0x22], io[0x23], io[0x24], io[0x25]);
        port_log("bg %llu cnt %04x %04x %04x %04x ofs %d %d %d %d %d %d %d %d", (unsigned long long) n, io[4], io[5], io[6],
                 io[7], io[8], io[9], io[10], io[11], io[12], io[13], io[14], io[15]);
    }
    static PortInput *in;
    if (!in) in = host_low_alloc(sizeof(*in));
    memset(in, 0, sizeof(*in));
    for (int i = 0; i < s_press_count; i++) {
        if (n >= s_presses[i].frame && n < s_presses[i].frame + s_presses[i].length) {
            in->buttons |= s_presses[i].buttons;
            if (s_presses[i].touch) {
                in->touching = true;
                in->touch_x = s_presses[i].x;
                in->touch_y = s_presses[i].y;
            }
        }
    }
    guest_input_set(in);
    s_audio_accum += 48000.0 / 59.8261;
    int samples = (int) s_audio_accum;
    s_audio_accum -= samples;
    static int16_t audio[2048 * 2];
    guest_snd_render(audio, samples, 48000);
    if (s_wav) {
        fwrite(audio, 4, (size_t) samples, s_wav);
        s_wav_frames += (uint32_t) samples;
    }
    if (s_dump_every && n % s_dump_every == 0 && (!getenv("MKDS_DUMP_FROM") || n >= (uint64_t) atoi(getenv("MKDS_DUMP_FROM")))) {
        char path[512];
        snprintf(path, sizeof(path), "%s/frame_%05llu.png", s_out, (unsigned long long) n);
        host_write_frame(f, path);
        port_log("frame %llu (scene %d, capcnt %02x%02x%02x%02x): DISPCNT A %08x B %08x, 3D %u polygons, POWCNT %04x", (unsigned long long) n, f->scene, f->io[0x67], f->io[0x66], f->io[0x65], f->io[0x64],
                 *(uint32_t *) f->io, *(uint32_t *) (f->io + 0x1000), f->frame3d.polygon_count,
                 *(uint16_t *) (f->io + 0x304));
    }
    return n < s_frames;
}

static void parse_press(const char *spec) {
    Press p = {0};
    char buttons[64] = "";
    unsigned long long frame = 0, length = 4;
    if (sscanf(spec, "%llu:%63[^:]:%llu", &frame, buttons, &length) < 2) {
        port_fatal("bad --press %s", spec);
    }
    p.frame = frame;
    p.length = length;
    if (buttons[0] == 't') {
        unsigned x, y;
        sscanf(buttons + 1, "%u,%u", &x, &y);
        p.touch = true;
        p.x = (uint16_t) x;
        p.y = (uint16_t) y;
    } else {
        for (const char *c = buttons; *c; c++) {
            switch (*c) {
            case 'A': p.buttons |= PAD_A; break;
            case 'B': p.buttons |= PAD_B; break;
            case 's': p.buttons |= PAD_SELECT; break;
            case 'S': p.buttons |= PAD_START; break;
            case 'R': p.buttons |= PAD_R; break;
            case 'L': p.buttons |= PAD_L; break;
            case 'u': p.buttons |= PAD_UP; break;
            case 'd': p.buttons |= PAD_DOWN; break;
            case 'l': p.buttons |= PAD_LEFT; break;
            case 'r': p.buttons |= PAD_RIGHT; break;
            case 'X': p.buttons |= PAD_X; break;
            case 'Y': p.buttons |= PAD_Y; break;
            }
        }
    }
    if (s_press_count < 256) {
        s_presses[s_press_count++] = p;
    }
}

// MKDS_FINISH_AT=frame: every kart reaches the goal at that frame (tests: to get past a race quickly).
#ifndef PORT_IPC
// Game-side symbols: not reachable in the arm64 split build (weak).
extern void (*g_port_vblank_hook)(void) __attribute__((weak));
extern uint64_t g_frame_count __attribute__((weak));
void FinishRaceProgressKartWithEndKind(int32_t kartIndex, int32_t endKind) __attribute__((weak));
static uint64_t s_finish_at;
static void finish_hook(void) {
    if (s_finish_at && g_frame_count == s_finish_at) {
        for (int k = 0; k < 8; k++) FinishRaceProgressKartWithEndKind(k, 0);
        port_log("test: race finished");
    }
}
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s rom.nds [--frames N] [--dump-every K] [--out DIR] [--realtime] [--press F:BTN[:LEN]]\n",
                argv[0]);
        return 1;
    }
    uint32_t rate = 0;
    const char *save = "mkds.sav";
#ifndef PORT_IPC
    if (getenv("MKDS_FINISH_AT") && &g_port_vblank_hook) {
        s_finish_at = strtoull(getenv("MKDS_FINISH_AT"), NULL, 0);
        g_port_vblank_hook = finish_hook;
    }
#endif
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) s_frames = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) s_dump_every = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) s_out = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) parse_press(argv[++i]);
        else if (!strcmp(argv[i], "--realtime")) rate = 59826;
        else if (!strcmp(argv[i], "--bglog")) s_bglog = true;
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) guest_set_language(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--wide") && i + 1 < argc) {
            extern float g_host_aspect;
            g_host_aspect = (float) atof(argv[++i]);
            s_wide_fx = (int32_t) (g_host_aspect * 4096 + 0.5f);
        }
        else if (!strcmp(argv[i], "--keyon")) {
            extern bool g_snd_keyon_log __attribute__((weak)); // not reachable in the arm64 split build
            if (&g_snd_keyon_log) g_snd_keyon_log = true;
            extern bool g_snd_chlog __attribute__((weak));
            if (&g_snd_chlog && getenv("MKDS_CHLOG")) g_snd_chlog = true;
        }
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
            s_wav = fopen(argv[++i], "wb");
            wav_header(s_wav, 0);
        }
    }
    mkdir(s_out, 0755);
#ifdef PORT_IPC
    // Two processes, as on arm64 Android: mkds_server (next to this program) runs the game in real time.
    (void) rate;
    char exe[600];
    snprintf(exe, sizeof(exe), "%s", argv[0]);
    char *slash = strrchr(exe, '/');
    snprintf(slash ? slash + 1 : exe, sizeof(exe) - (size_t) (slash ? slash + 1 - exe : 0), "mkds_server");
    if (!guest_load()) port_fatal("cannot create the shared memory");
    const char *err = ipc_start(exe, argv[1], save, ".", 59826);
    if (err) port_fatal("%s", err);
    uint64_t last = 0;
    for (;;) {
        const FrameSnapshot *f = guest_frame_acquire();
        if (!f || f->number <= last) {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
            continue;
        }
        if (f->number > last + 1 && last) port_log("ipc: skipped %llu frames", (unsigned long long) (f->number - last - 1));
        last = f->number;
        if (!on_frame(f, f->number - 1)) {
            port_log("done after %llu frames", (unsigned long long) s_frames);
            guest_save_flush();
            if (s_wav) {
                wav_header(s_wav, s_wav_frames);
                fclose(s_wav);
            }
            exit(0);
        }
    }
#endif
    if (!port_mem_init() || !guest_load()) {
        port_fatal("cannot reserve the DS address space / load the game");
    }
    if (!port_rom_open(argv[1])) {
        return 1;
    }
    port_save_set_path(save);
    guest_set_vblank_rate(rate);
    if (s_wide_fx) guest_set_aspect(s_wide_fx);
    void *ready = hapi_sem_new(), *done = hapi_sem_new();
    guest_set_lockstep(ready, done);
    guest_start_game();
    for (;;) {
        hapi_sem_wait(ready);
        const FrameSnapshot *f = guest_frame_acquire();
        bool more = !f || on_frame(f, f->number - 1);
        if (!more) {
            port_log("done after %llu frames", (unsigned long long) s_frames);
            guest_save_flush();
            if (s_wav) {
                wav_header(s_wav, s_wav_frames);
                fclose(s_wav);
            }
            exit(0);
        }
        hapi_sem_post(done);
    }
    return 0;
}

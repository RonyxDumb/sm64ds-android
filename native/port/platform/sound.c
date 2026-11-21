// The ARM7 half of the NitroSDK sound system, written for the host.
//
// The game's SND library (ARM9 side, from the decomp) sends lists of commands through the PXI FIFO (tag 7). They are
// carried out at once on the game thread and reported finished in the shared work area. The sequence player runs on
// the audio thread: every 5.2 ms of audio (the DS sound driver's frame) it advances the sequences (SSEQ), the
// envelopes and the LFOs, then the 16 channels are mixed at the output rate. One mutex guards the driver state.
//
// Formats: SSEQ event streams, SBNK instruments (PCM, PSG, noise, drum sets, key splits), SWAR/SWAV wave data (PCM8,
// PCM16, IMA-ADPCM).
#include "port.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "hostapi.h"
#include <string.h>

#include "types.h"
#include "sound_command_queue.h"
#include "sound_shared_work_access.h"
#include "arm_fifo_messaging.h"
#include "alarm_timer.h"
#include "tick_counter.h"

#define PLAYERS 16
#define TRACKS 32
#define CHANNELS 16
#define ENV_MIN (-92544) // envelope floor, 0.1 dB * 128
#define FRAME_SECONDS (64.0 * 2728.0 / 33513982.0) // sound driver frame: 5.2 ms

static void *s_lock;
static const bool s_debug = false; // log notes and sequences
bool g_snd_keyon_log;                // tests: log each note's channel registers (compared with a reference)
bool g_snd_chlog;                    // tests: log every active channel once per video frame

// Output effect. The game captures the mixer's output (capture units, channels 1 and 3 replay it) and processes it
// on the ARM9 (NNS_SndCaptureStartOutputEffect: surround by default, headphones or mono from the options), and the
// surround also lowers the channels panned to the sides (SNDi_SetSurroundDecay). Here the same processing is applied
// to the output directly, the type read from the game's state.
#include "capture_output_effect.h"
extern CallbackInfo sCallbackInfo;
static int s_surround_decay;
static void snd_lock(void) {
    if (!s_lock) s_lock = hapi_mutex_new();
    hapi_mutex_lock(s_lock);
}
static SNDSharedWork *s_shared;
static int s_output_rate = 48000;
static double s_frame_accum;
static int s_master_volume = 127;

// ------------------------------------------------------------------------------------------------ tables
static int16_t s_decibel_sq[128]; // velocity / volume (0..127) to 0.1 dB, squared curve (SND_DecibelSquareTable)
static const uint8_t s_attack_table[19] = {0, 1, 5, 14, 26, 38, 51, 63, 73, 84, 92, 100, 109, 116, 123, 127, 132, 137, 143};
static const int8_t s_sine_table[33] = {0,  6,  12, 19, 25, 31, 37, 43, 49, 54,  60,  65,  71,  76,  81,  85, 90,
                                        94, 98, 102, 106, 109, 112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127};

static void init_tables(void) {
    s_decibel_sq[0] = -32768 / 45; // effectively silent
    for (int i = 1; i < 128; i++) {
        double db10 = 400.0 * log10((double) i / 127.0); // 20*log10((i/127)^2) in 0.1 dB
        s_decibel_sq[i] = (int16_t) (db10 < -723 ? -723 : db10);
    }
}

static int sine(int x) { // x in 0..127 -> -127..127
    x &= 127;
    if (x < 32) return s_sine_table[x];
    if (x < 64) return s_sine_table[64 - x];
    if (x < 96) return -s_sine_table[x - 64];
    return -s_sine_table[128 - x];
}

static int attack_rate(int a) { return a < 109 ? 255 - a : s_attack_table[127 - a]; }
static int decay_rate(int x) {
    if (x == 127) return 0xffff;
    if (x == 126) return 0x3c00;
    if (x < 50) return x * 2 + 1;
    return 0x1e00 / (126 - x);
}

// ------------------------------------------------------------------------------------------------ state
typedef struct Lfo {
    uint8_t target, speed, depth, range;
    uint16_t delay;
    uint16_t delay_counter, counter;
} Lfo;

enum { CH_FREE, CH_PCM, CH_PSG, CH_NOISE };
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct Channel {
    int type;
    bool active, sequenced, locked;
    // sample source
    const uint8_t *data;
    int format; // 0 PCM8, 1 PCM16, 2 ADPCM
    bool loop;
    uint32_t loop_start, loop_len; // in samples
    double pos;                     // in samples
    int duty;
    uint16_t lfsr;
    // ADPCM
    int adpcm_sample, adpcm_index, adpcm_pos;
    int loop_sample, loop_index;
    bool loop_saved;
    bool logged;
    int last_sample;
    // pitch / volume
    uint16_t base_timer;
    int key, base_key, velocity;
    int pitch_offset; // 1/64 semitone
    int sweep_pitch, sweep_length, sweep_counter;
    uint64_t chlog_frame;
    int env_state, env_level;
    int attack, decay, sustain, release;
    int length; // remaining ticks of the note, -1 = until released
    int priority;
    int user_db;    // track/player volume in 0.1 dB (the last ones, once the track has let go)
    int user_pitch; // track pitch bend and pitch, 1/64 semitone
    int last_db;    // the volume of the last update, 0.1 dB (the quietest note gives its channel away first)
    int pan, user_pan;
    Lfo lfo;
    int track; // owning track, -1 none
    // direct (non-sequenced) settings
    int direct_volume, direct_shift, direct_pan;
    uint16_t direct_timer;
    // mix output
    float gain_l, gain_r;
    double step;
} Channel;

typedef struct Track {
    bool active, note_wait, note_finish_wait, mute, tie, porta, cmp, mask_set;
    int player;
    uint16_t program;
    int volume, volume2;
    int pitch_bend, bend_range;
    int pan, ext_pan;
    int ext_fader, ext_pitch;
    int attack, decay, sustain, release;
    int prio, transpose, porta_key, porta_time, sweep_pitch;
    Lfo lfo;
    uint16_t channel_mask;
    int32_t wait;
    const uint8_t *base, *cur;
    const uint8_t *call_stack[3];
    uint8_t loop_count[3];
    int depth;
} Track;

typedef struct Player {
    bool active, prepared, paused;
    int prio, volume, ext_fader;
    int8_t tracks[16];
    uint16_t tempo, tempo_ratio, tempo_counter;
    const uint8_t *bank;
    uint32_t ticks;
} Player;

static Channel s_ch[CHANNELS];
static Track s_tracks[TRACKS];
static Player s_players[PLAYERS];
static uint32_t s_locked_channels;

// ------------------------------------------------------------------------------------------------ data access
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t) (p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }

// Wave `index` of the wave archive linked to the bank as archive `arc` (SND_GetWaveDataAddress).
static const uint8_t *bank_wave(const uint8_t *bank, int arc, int index) {
    const uint8_t *swar = (const uint8_t *) (uintptr_t) rd32(bank + 0x18 + arc * 8);
    if (!swar) return NULL;
    if ((uint32_t) index >= rd32(swar + 0x38)) return NULL;
    uint32_t off = rd32(swar + 0x3c + index * 4);
    if (!off) return NULL;
    return off < 0x2000000 ? swar + off : (const uint8_t *) (uintptr_t) off;
}

// The 10 bytes describing one instrument region: wave (or duty), archive, base key, ADSR, pan.
typedef struct Inst {
    int type;
    const uint8_t *param;
} Inst;

static bool bank_instrument(const uint8_t *bank, int program, int key, Inst *out) {
    if (!bank) return false;
    uint32_t count = rd32(bank + 0x38);
    if ((uint32_t) program >= count) return false;
    uint32_t rec = rd32(bank + 0x3c + program * 4);
    int type = rec & 0xff;
    const uint8_t *data = bank + (rec >> 8);
    switch (type) {
    case 1: case 2: case 3: case 4: case 5:
        out->type = type;
        out->param = data;
        return true;
    case 16: { // drum set: low, high, then 12-byte entries (type u16 + 10 bytes)
        int low = data[0], high = data[1];
        if (key < low || key > high) return false;
        const uint8_t *e = data + 2 + (key - low) * 12;
        out->type = rd16(e);
        out->param = e + 2;
        return out->type != 0;
    }
    case 17: { // key split: 8 region upper keys, then 12-byte entries
        int region = 0;
        while (region < 8 && data[region] && key > data[region]) region++;
        if (region >= 8 || !data[region]) return false;
        const uint8_t *e = data + 8 + region * 12;
        out->type = rd16(e);
        out->param = e + 2;
        return out->type != 0;
    }
    default:
        return false;
    }
}

// ------------------------------------------------------------------------------------------------ channels
static void channel_stop(Channel *c) {
    c->active = false;
    c->type = CH_FREE;
    c->track = -1;
}

// Timers are periods in ARM7 clocks / 2 (the SDK's convention; the register takes 0x10000 minus it). A pitch in 1/64
// semitone shortens the period by 2^(pitch / 768).
static double rate_for(uint16_t period, int pitch) {
    double t = period * pow(2.0, -pitch / 768.0);
    if (t < 16) t = 16;
    return 16756991.0 / t;
}

static const int8_t s_ima_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t s_ima_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static int adpcm_next(Channel *c) {
    const uint8_t *d = c->data + 4; // after the 4-byte header (initial sample and index)
    int nibble = (d[c->adpcm_pos >> 1] >> ((c->adpcm_pos & 1) * 4)) & 15;
    int step = s_ima_step[c->adpcm_index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) {
        c->adpcm_sample -= diff;
        if (c->adpcm_sample < -32767) c->adpcm_sample = -32767;
    } else {
        c->adpcm_sample += diff;
        if (c->adpcm_sample > 32767) c->adpcm_sample = 32767;
    }
    c->adpcm_index += s_ima_index[nibble & 7];
    if (c->adpcm_index < 0) c->adpcm_index = 0;
    if (c->adpcm_index > 88) c->adpcm_index = 88;
    c->adpcm_pos++;
    return c->adpcm_sample;
}

// Reads the sample at the current position (advancing the ADPCM decoder as needed). Returns false at the end.
static bool channel_sample(Channel *c, int *out) {
    if (c->type == CH_PSG) {
        int phase = (int) c->pos & 7; // one duty step per timer period, 8 per cycle
        *out = phase < c->duty + 1 ? 0x7fff : -0x7fff;
        return true;
    }
    if (c->type == CH_NOISE) {
        *out = c->lfsr & 1 ? -0x7fff : 0x7fff;
        return true;
    }
    uint32_t i = (uint32_t) c->pos;
    uint32_t end = c->loop_start + c->loop_len;
    if (i >= end) {
        if (!c->loop || c->loop_len == 0) return false;
        uint32_t over = (i - c->loop_start) % c->loop_len;
        c->pos = c->loop_start + over + (c->pos - (double) i);
        i = c->loop_start + over;
        if (c->format == 2) {
            c->adpcm_sample = c->loop_sample;
            c->adpcm_index = c->loop_index;
            c->adpcm_pos = (int) c->loop_start;
        }
    }
    switch (c->format) {
    case 0: *out = (int8_t) c->data[i] << 8; break;
    case 1: *out = (int16_t) rd16(c->data + i * 2); break;
    default:
        while ((uint32_t) c->adpcm_pos <= i) {
            if ((uint32_t) c->adpcm_pos == c->loop_start && !c->loop_saved) {
                c->loop_sample = c->adpcm_sample;
                c->loop_index = c->adpcm_index;
                c->loop_saved = true;
            }
            c->last_sample = adpcm_next(c);
        }
        *out = c->last_sample;
        break;
    }
    return true;
}

static void channel_start_wave(Channel *c, const uint8_t *swav) {
    // SWAV header: format, loop, rate, timer, loop start (words), loop length (words)
    c->format = swav[0];
    c->loop = swav[1];
    c->base_timer = rd16(swav + 4);
    uint32_t loop_start_words = rd16(swav + 6), loop_len_words = rd32(swav + 8);
    c->data = swav + 12;
    int per_word = c->format == 0 ? 4 : c->format == 1 ? 2 : 8;
    if (c->format == 2) {
        // The first word is the ADPCM header (initial sample, index).
        c->adpcm_sample = (int16_t) rd16(c->data);
        c->adpcm_index = c->data[2] & 0x7f;
        if (c->adpcm_index > 88) c->adpcm_index = 88;
        c->adpcm_pos = 0;
        c->loop_saved = false;
        c->loop_start = loop_start_words ? (loop_start_words - 1) * 8 : 0;
        c->loop_len = loop_len_words * 8;
    } else {
        c->loop_start = loop_start_words * per_word;
        c->loop_len = loop_len_words * per_word;
    }
    c->pos = 0;
}

// ------------------------------------------------------------------------------------------------ tracks
static int track_index(const Track *t) { return (int) (t - s_tracks); }

static void track_release_channels(Track *t, int release) {
    int ti = track_index(t);
    for (int i = 0; i < CHANNELS; i++) {
        Channel *c = &s_ch[i];
        if (c->active && c->sequenced && c->track == ti) {
            if (release >= 0) c->release = decay_rate(release);
            c->env_state = ENV_RELEASE;
            c->priority = 1;
            c->length = -1;
        }
    }
}

static void track_stop(Track *t, bool kill) {
    int ti = track_index(t);
    for (int i = 0; i < CHANNELS; i++) {
        Channel *c = &s_ch[i];
        if (c->active && c->sequenced && c->track == ti) {
            if (kill) {
                channel_stop(c);
            } else {
                c->env_state = ENV_RELEASE;
                c->priority = 1;
                c->length = -1;
                c->track = -1;
            }
        }
    }
    t->active = false;
}

static void track_init(Track *t, int player, const uint8_t *base, const uint8_t *cur) {
    memset(t, 0, sizeof(*t));
    t->active = true;
    t->player = player;
    t->note_wait = true;
    t->base = base;
    t->cur = cur;
    t->volume = 127;
    t->volume2 = 127;
    t->bend_range = 2;
    t->prio = 64;
    t->attack = t->decay = t->sustain = t->release = 0xff;
    t->porta_key = 60;
    t->lfo.speed = 16;
    t->lfo.range = 1;
    t->channel_mask = 0xffff;
}

static int alloc_track(void) {
    for (int i = 0; i < TRACKS; i++) {
        if (!s_tracks[i].active) return i;
    }
    return -1;
}

static void player_stop(int p, bool kill) {
    Player *pl = &s_players[p];
    for (int i = 0; i < 16; i++) {
        if (pl->tracks[i] >= 0) {
            track_stop(&s_tracks[pl->tracks[i]], kill);
            pl->tracks[i] = -1;
        }
    }
    pl->active = pl->prepared = false;
}

// Channel for a note: a free allowed channel, else the lowest-priority one below `prio`.
static Channel *alloc_channel(int type, uint16_t mask, int prio) {
    static const uint8_t pcm_order[] = {4, 5, 6, 7, 2, 0, 3, 1, 8, 9, 10, 11, 14, 12, 15, 13};
    Channel *best = NULL;
    for (int k = 0; k < 16; k++) {
        int i = pcm_order[k];
        if (type == CH_PSG && (i < 8 || i > 13)) continue;
        if (type == CH_NOISE && i < 14) continue;
        if (!(mask & (1u << i)) || (s_locked_channels & (1u << i))) continue;
        Channel *c = &s_ch[i];
        if (!c->active) return c;
        if (c->sequenced && (!best || c->priority < best->priority || (c->priority == best->priority &&
                                                                       c->last_db < best->last_db))) {
            best = c;
        }
    }
    if (best && best->priority <= prio) {
        return best;
    }
    return NULL;
}

static void note_on(Track *t, int key, int velocity, int length) {
    Player *pl = &s_players[t->player];
    Inst inst;
    // Tie mode: notes are joined, a note does not end with its length (the next one takes its channel over).
    if (t->tie) length = -1;
    if (!bank_instrument(pl->bank, t->program, key, &inst)) return;
    const uint8_t *p = inst.param;
    if (inst.type == 5) return; // null instrument
    int type = inst.type == 2 ? CH_PSG : inst.type == 3 ? CH_NOISE : CH_PCM;
    int prio = t->prio + pl->prio;
    if (prio > 255) prio = 255;
    Channel *c = NULL;
    // Tie mode: reuse the track's sounding channel.
    if (t->tie) {
        for (int i = 0; i < CHANNELS; i++) {
            if (s_ch[i].active && s_ch[i].sequenced && s_ch[i].track == track_index(t)) {
                c = &s_ch[i];
                break;
            }
        }
        if (c) {
            c->key = key;
            c->velocity = velocity;
            c->length = length;
            return;
        }
    }
    uint16_t mask = t->mask_set ? t->channel_mask : 0xffff;
    c = alloc_channel(type, mask, prio);
    if (!c) {
        if (g_snd_keyon_log) {
            char st[200]; int n = 0;
            for (int i = 0; i < CHANNELS; i++) n += snprintf(st + n, sizeof(st) - n, " %d:%d/%d/%d", i, s_ch[i].active, s_ch[i].sequenced, s_ch[i].priority);
            port_log("keyfail %llu key %d prio %d mask %04x locked %04x%s", (unsigned long long) g_frame_count, key, prio, mask, s_locked_channels, st);
        }
        return;
    }
    memset(c, 0, sizeof(*c));
    c->last_db = 0; // a new note counts as loud until its first update
    c->type = type;
    if (type == CH_PCM) {
        // PCM: wave index and archive; direct PCM: the wave's address.
        const uint8_t *swav = inst.type == 4 ? (const uint8_t *) (uintptr_t) rd32(p) : bank_wave(pl->bank, rd16(p + 2), rd16(p));
        if (!swav) {
            if (g_snd_keyon_log) {
                const uint8_t *swar = (const uint8_t *) (uintptr_t) rd32(pl->bank + 0x18 + rd16(p + 2) * 8);
                port_log("nowave bank %p arc %d index %d swar %p count %u off %08x", (void *) pl->bank, rd16(p + 2), rd16(p),
                         (void *) swar, swar ? rd32(swar + 0x38) : 0, swar ? rd32(swar + 0x3c + rd16(p) * 4) : 0);
            }
            channel_stop(c);
            return;
        }
        channel_start_wave(c, swav);
    } else if (type == CH_PSG) {
        c->duty = rd16(p);
        c->base_timer = (uint16_t) (16756991.0 / (8 * 261.63)); // one 8-step cycle per period at C4
    } else {
        c->lfsr = 0x7fff;
        c->base_timer = (uint16_t) (16756991.0 / 261.63 / 8);
    }
    if (s_debug) {
        port_log("note p%d prog %d key %d vel %d len %d inst %d type %d fmt %d loop %d timer %04x ls %u ll %u swav %p",
                 t->player, t->program, key, velocity, length, inst.type, type, c->format, c->loop, c->base_timer,
                 c->loop_start, c->loop_len, (void *) c->data);
    }
    c->active = true;
    c->sequenced = true;
    c->track = track_index(t);
    c->key = key;
    c->base_key = p[4];
    c->velocity = velocity;
    c->length = length;
    c->priority = prio;
    c->attack = attack_rate(t->attack != 0xff ? t->attack : p[5]);
    c->decay = decay_rate(t->decay != 0xff ? t->decay : p[6]);
    c->sustain = s_decibel_sq[t->sustain != 0xff ? t->sustain : p[7]] << 7;
    c->release = decay_rate(t->release != 0xff ? t->release : p[8]);
    c->pan = p[9] - 64;
    c->env_state = ENV_ATTACK;
    c->env_level = ENV_MIN;
    c->lfo = t->lfo;
    c->lfo.delay_counter = 0;
    c->lfo.counter = 0;
    if (t->porta) {
        int from = t->porta_key;
        c->sweep_pitch = (from - key) * 64 + t->sweep_pitch;
        int time = t->porta_time;
        c->sweep_length = time ? (time * time * abs(c->sweep_pitch)) >> 11 : length;
        c->sweep_counter = 0;
    } else {
        c->sweep_pitch = t->sweep_pitch;
        c->sweep_length = length;
        c->sweep_counter = 0;
    }
    t->porta_key = key;
}

static uint32_t read_varlen(const uint8_t **p) {
    uint32_t v = 0;
    uint8_t b;
    do {
        b = *(*p)++;
        v = (v << 7) | (b & 0x7f);
    } while (b & 0x80);
    return v;
}

enum { ARG_U8, ARG_S16, ARG_VARLEN, ARG_RANDOM, ARG_VARIABLE };

static int16_t *variable(Track *t, int n) {
    if (!s_shared) return NULL;
    if (n < 16) return &s_shared->mPlayer[t->player].mVariable[n];
    if (n < 32) return &s_shared->mGlobalVariable[n - 16];
    return NULL;
}

static int32_t read_arg(Track *t, int type) {
    switch (type) {
    case ARG_U8: return *t->cur++;
    case ARG_S16: {
        int16_t v = (int16_t) rd16(t->cur);
        t->cur += 2;
        return v;
    }
    case ARG_VARLEN: return (int32_t) read_varlen(&t->cur);
    case ARG_RANDOM: {
        int16_t lo = (int16_t) rd16(t->cur), hi = (int16_t) rd16(t->cur + 2);
        t->cur += 4;
        return lo + rand() % (hi - lo + 1);
    }
    default: {
        int16_t *v = variable(t, *t->cur++);
        return v ? *v : 0;
    }
    }
}

// Runs the track until it waits. Returns false when the track ended.
static bool track_step(Track *t) {
    Player *pl = &s_players[t->player];
    if (t->wait > 0) {
        t->wait--;
        if (t->wait > 0) return true;
    }
    if (t->note_finish_wait) {
        // A note of length 0 with note wait: the track waits until it has finished sounding.
        for (int i = 0; i < CHANNELS; i++) {
            if (s_ch[i].active && s_ch[i].sequenced && s_ch[i].track == track_index(t)) return true;
        }
        t->note_finish_wait = false;
    }
    for (int guard = 0; guard < 10000 && t->wait == 0 && !t->note_finish_wait; guard++) {
        uint8_t cmd = *t->cur++;
        int arg_type = -1;
        bool run = true;
        if (cmd == 0xa2) { // if
            cmd = *t->cur++;
            run = t->cmp;
        }
        if (cmd == 0xa0) { // random argument
            cmd = *t->cur++;
            arg_type = ARG_RANDOM;
        } else if (cmd == 0xa1) { // variable argument
            cmd = *t->cur++;
            arg_type = ARG_VARIABLE;
        }
        if (cmd < 0x80) { // note: velocity, length
            int velocity = *t->cur++;
            int length = arg_type >= 0 ? read_arg(t, arg_type) : (int) read_varlen(&t->cur);
            int key = cmd + t->transpose;
            if (key < 0) key = 0;
            if (key > 127) key = 127;
            if (run && !t->mute) note_on(t, key, velocity, length > 0 ? length : -1);
            t->porta_key = key;
            if (t->note_wait && run) {
                t->wait = length;
                if (length == 0) t->note_finish_wait = true;
            }
            continue;
        }
        switch (cmd & 0xf0) {
        case 0x80: {
            int32_t v = arg_type >= 0 ? read_arg(t, arg_type) : (int32_t) read_varlen(&t->cur);
            if (!run) break;
            if (cmd == 0x80) t->wait = v;
            else if (cmd == 0x81 && v < 0x10000) t->program = (uint16_t) v;
            break;
        }
        case 0x90: {
            if (cmd == 0x93) { // open track: number, 24-bit offset
                int no = *t->cur++;
                uint32_t off = t->cur[0] | t->cur[1] << 8 | t->cur[2] << 16;
                t->cur += 3;
                if (!run || no >= 16) break;
                int ti = pl->tracks[no];
                if (ti < 0) {
                    ti = alloc_track();
                    if (ti < 0) break;
                    pl->tracks[no] = (int8_t) ti;
                }
                track_init(&s_tracks[ti], t->player, t->base, t->base + off);
            } else if (cmd == 0x94 || cmd == 0x95) { // jump / call
                uint32_t off = t->cur[0] | t->cur[1] << 8 | t->cur[2] << 16;
                t->cur += 3;
                if (!run) break;
                if (cmd == 0x95) {
                    if (t->depth >= 3) break;
                    t->call_stack[t->depth++] = t->cur;
                }
                t->cur = t->base + off;
            }
            break;
        }
        case 0xb0: { // variables: number, value
            int n = *t->cur++;
            int32_t v = arg_type >= 0 ? read_arg(t, arg_type) : read_arg(t, ARG_S16);
            int16_t *var = variable(t, n);
            if (!run || !var) break;
            switch (cmd) {
            case 0xb0: *var = (int16_t) v; break;
            case 0xb1: *var = (int16_t) (*var + v); break;
            case 0xb2: *var = (int16_t) (*var - v); break;
            case 0xb3: *var = (int16_t) (*var * v); break;
            case 0xb4: if (v) *var = (int16_t) (*var / v); break;
            case 0xb5: *var = (int16_t) (v >= 0 ? *var << v : *var >> -v); break;
            case 0xb6: *var = (int16_t) (v < 0 ? -(rand() % (-v + 1)) : rand() % (v + 1)); break;
            case 0xb8: t->cmp = *var == v; break;
            case 0xb9: t->cmp = *var >= v; break;
            case 0xba: t->cmp = *var > v; break;
            case 0xbb: t->cmp = *var <= v; break;
            case 0xbc: t->cmp = *var < v; break;
            case 0xbd: t->cmp = *var != v; break;
            }
            break;
        }
        case 0xc0:
        case 0xd0: {
            int32_t v = arg_type >= 0 ? read_arg(t, arg_type) : *t->cur++;
            if (!run) break;
            switch (cmd) {
            case 0xc0: t->pan = v - 64; break;
            case 0xc1: t->volume = v; break;
            case 0xc2: pl->volume = v; break;
            case 0xc3: t->transpose = (int8_t) v; break;
            case 0xc4: t->pitch_bend = (int8_t) v; break;
            case 0xc5: t->bend_range = v; break;
            case 0xc6: t->prio = v; break;
            case 0xc7: t->note_wait = v & 1; break;
            case 0xc8: t->tie = v & 1; track_release_channels(t, -1); break;
            case 0xc9: t->porta_key = v + t->transpose; t->porta = true; break;
            case 0xca: t->lfo.depth = (uint8_t) v; break;
            case 0xcb: t->lfo.speed = (uint8_t) v; break;
            case 0xcc: t->lfo.target = (uint8_t) v; break;
            case 0xcd: t->lfo.range = (uint8_t) v; break;
            case 0xce: t->porta = v & 1; break;
            case 0xcf: t->porta_time = v; break;
            case 0xd0: t->attack = v; break;
            case 0xd1: t->decay = v; break;
            case 0xd2: t->sustain = v; break;
            case 0xd3: t->release = v; break;
            case 0xd4: // loop start
                if (t->depth < 3) {
                    t->call_stack[t->depth] = t->cur;
                    t->loop_count[t->depth] = (uint8_t) v;
                    t->depth++;
                }
                break;
            case 0xd5: t->volume2 = v; break;
            case 0xd7: t->mute = v & 1; break;
            default: break;
            }
            break;
        }
        case 0xe0: {
            int32_t v = arg_type >= 0 ? read_arg(t, arg_type) : read_arg(t, ARG_S16);
            if (!run) break;
            if (cmd == 0xe0) t->lfo.delay = (uint16_t) v;
            else if (cmd == 0xe1) pl->tempo = (uint16_t) v;
            else if (cmd == 0xe3) t->sweep_pitch = (int16_t) v;
            break;
        }
        case 0xf0:
            if (cmd == 0xfc) { // loop end
                if (!run || t->depth == 0) break;
                uint8_t *count = &t->loop_count[t->depth - 1];
                if (*count == 0) { // infinite
                    t->cur = t->call_stack[t->depth - 1];
                } else if (--*count == 0) {
                    t->depth--;
                } else {
                    t->cur = t->call_stack[t->depth - 1];
                }
            } else if (cmd == 0xfd) { // return
                if (run && t->depth) t->cur = t->call_stack[--t->depth];
            } else if (cmd == 0xfe) { // track allocation mask
                t->cur += 2;
            } else if (cmd == 0xff) { // end
                if (run) return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

static void player_tick(int p) {
    Player *pl = &s_players[p];
    bool any = false;
    for (int i = 0; i < 16; i++) {
        int ti = pl->tracks[i];
        if (ti < 0) continue;
        Track *t = &s_tracks[ti];
        if (!t->active) continue;
        if (!track_step(t)) {
            track_stop(t, false);
            pl->tracks[i] = -1;
            continue;
        }
        any = true;
    }
    // Note lengths count down in ticks, and so do the sweeps (portamento) of sequenced notes.
    for (int i = 0; i < CHANNELS; i++) {
        Channel *c = &s_ch[i];
        if (c->active && c->sequenced && c->track >= 0 && s_tracks[c->track].player == p &&
            c->sweep_counter < c->sweep_length) {
            c->sweep_counter++;
        }
        if (c->active && c->sequenced && c->track >= 0 && s_tracks[c->track].player == p && c->length > 0) {
            if (--c->length == 0) {
                c->env_state = ENV_RELEASE;
                c->priority = 1; // a released note is the first to give its channel away
                c->length = -1;
            }
        }
    }
    pl->ticks++;
    if (s_shared) s_shared->mPlayer[p].mTickCounter = pl->ticks;
    if (!any) {
        player_stop(p, false);
    }
}

// ------------------------------------------------------------------------------------------------ driver frame
static void update_channel_params(Channel *c) {
    if (!c->sequenced) {
        static const int shifts[4] = {0, 1, 2, 4};
        float vol = c->direct_volume / 127.0f / (float) (1 << shifts[c->direct_shift & 3]);
        float pan = (float) c->direct_pan;
        c->gain_l = vol * (128 - pan) / 128.0f;
        c->gain_r = vol * pan / 128.0f;
        double rate = c->direct_timer ? 16756991.0 / c->direct_timer : 0;
        c->step = rate / s_output_rate;
        return;
    }
    // Envelope.
    switch (c->env_state) {
    case ENV_ATTACK:
        c->env_level = -((c->attack * -c->env_level) >> 8); // rounds toward 0, so it reaches 0
        if (c->env_level == 0 || c->attack == 0) {
            c->env_level = 0;
            c->env_state = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        c->env_level -= c->decay;
        if (c->env_level <= c->sustain) {
            c->env_level = c->sustain;
            c->env_state = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        break;
    case ENV_RELEASE:
        c->env_level -= c->release;
        if (c->env_level <= ENV_MIN) {
            channel_stop(c);
            return;
        }
        break;
    }
    Track *t = c->track >= 0 ? &s_tracks[c->track] : NULL;
    Player *pl = t ? &s_players[t->player] : NULL;
    // Volume in 0.1 dB.
    int db = (c->env_level >> 7) + s_decibel_sq[c->velocity & 127];
    int pitch = (c->key - c->base_key) * 64;
    int pan = c->pan;
    if (t) {
        // Kept for after the track lets the channel go (it fades out with the track's volume, pitch and pan).
        c->user_db = s_decibel_sq[t->volume & 127] + s_decibel_sq[t->volume2 & 127] + t->ext_fader +
                     s_decibel_sq[pl->volume & 127] + pl->ext_fader;
        c->user_pitch = t->pitch_bend * t->bend_range * 64 / 128 + t->ext_pitch;
        c->user_pan = t->pan + t->ext_pan;
    }
    db += c->user_db;
    pitch += c->user_pitch;
    pan += c->user_pan;
    // Sweep / portamento.
    if (c->sweep_pitch && c->sweep_length > 0 && c->sweep_counter < c->sweep_length) {
        pitch += (int) ((int64_t) c->sweep_pitch * (c->sweep_length - c->sweep_counter) / c->sweep_length);
        if (!c->sequenced) c->sweep_counter++; // the others sweep per driver frame
    }
    // LFO: the parameters follow the track, the counters are the channel's.
    Lfo *l = &c->lfo;
    if (t) {
        l->target = t->lfo.target;
        l->speed = t->lfo.speed;
        l->depth = t->lfo.depth;
        l->range = t->lfo.range;
        l->delay = t->lfo.delay;
    }
    if (l->depth) {
        if (l->delay_counter < l->delay) {
            l->delay_counter++;
        } else {
            l->counter = (uint16_t) (l->counter + (l->speed << 6));
            int v = sine(l->counter >> 8) * l->depth * l->range; // 127*127*range
            switch (l->target) {
            case 0: pitch += v >> 8; break;          // pitch (in 1/64 semitone)
            case 1: db += (v * 60) >> 14; break;     // volume
            case 2: pan += v >> 8; break;            // pan
            }
        }
    }
    // A released note ends as soon as it is inaudible (its channel is free again).
    if (c->env_state == ENV_RELEASE && db <= -723) {
        channel_stop(c);
        return;
    }
    if (db < -723) db = -723;
    if (db > 0) db = 0;
    c->last_db = db;
    float gain = powf(10.0f, db / 200.0f) * (s_master_volume / 127.0f);
    if (pan < -64) pan = -64;
    if (pan > 63) pan = 63;
    // The mixer pans linearly: left (128 - pan) / 128, right pan / 128, pan 0..127.
    float p = (float) (pan + 64);
    int ci = (int) (c - s_ch);
    if (s_surround_decay > 0 && ci != 1 && ci != 3) {
        // The ARM7's CalcSurroundDecay on the channel volume (0x7fff << 6 is about 1 << 21).
        int pn = pan + 64, d = s_surround_decay;
        if (pn < 24) gain *= (float) ((int64_t) d * (pn + 40) + ((int64_t) (0x7fff - d) << 6)) / 2097152.0f;
        else if (pn > 104) gain *= (float) (-(int64_t) d * (pn - 40) + ((int64_t) (d + 0x7fff) << 6)) / 2097152.0f;
    }
    c->gain_l = gain * (128 - p) / 128.0f;
    c->gain_r = gain * p / 128.0f;
    double rate = rate_for(c->base_timer, pitch);
    if (g_snd_chlog && c->chlog_frame != g_frame_count) {
        c->chlog_frame = g_frame_count;
        double period = c->base_timer * pow(2.0, -pitch / 768.0);
        port_log("chlog %llu ch%d src %07x tmr %04x db %d pan %d", (unsigned long long) g_frame_count, (int) (c - s_ch),
                 (unsigned) (uintptr_t) (c->data ? c->data : 0), (unsigned) (0x10000 - (int) period), db, pan + 64);
    }
    if (g_snd_keyon_log && !c->logged) {
        c->logged = true;
        double period = c->base_timer * pow(2.0, -pitch / 768.0);
        port_log("keyon %llu ch%d fmt%d src %07x tmr %04x vol %d pan %d p%d", (unsigned long long) g_frame_count,
                 (int) (c - s_ch), c->type == CH_PCM ? c->format : c->type == CH_PSG ? 3 : 4,
                 (unsigned) (uintptr_t) (c->data ? c->data : 0), (unsigned) (0x10000 - (int) period), db,
                 pan + 64, t ? t->player : -1);
    }
    c->step = rate / s_output_rate;
}

static void driver_frame(void) {
    for (int p = 0; p < PLAYERS; p++) {
        Player *pl = &s_players[p];
        if (!pl->active || pl->paused) continue;
        uint32_t step = (uint32_t) pl->tempo * pl->tempo_ratio / 256;
        pl->tempo_counter += step;
        while (pl->tempo_counter >= 240 && pl->active) {
            pl->tempo_counter -= 240;
            player_tick(p);
        }
    }
    uint32_t status = 0;
    for (int p = 0; p < PLAYERS; p++) {
        if (s_players[p].active) status |= 1u << p;
    }
    uint16_t chstat = 0;
    for (int i = 0; i < CHANNELS; i++) {
        if (s_ch[i].active) {
            update_channel_params(&s_ch[i]);
        }
        if (s_ch[i].active) chstat |= (uint16_t) (1u << i);
    }
    if (s_shared) {
        s_shared->mPlayerStatus = status;
        s_shared->mChannelStatus = chstat;
    }
}

// ------------------------------------------------------------------------------------------------ mixing
// The capture runs at 32000 Hz: its delays (2 samples for the surround, 24 for the headphones) at the output rate.
static float s_fx_hist[2][64];
static int s_fx_pos;

static inline float clamp16(float v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

static void output_effect(float *l, float *r, int rate) {
    int type = sCallbackInfo.mType;
    float L = clamp16(*l), R = clamp16(*r);
    int at = s_fx_pos;
    s_fx_pos = (s_fx_pos + 1) & 63;
    switch (type) {
    case SOUND_OUTPUT_EFFECT_SURROUND: { // L + (L - R) two samples before, R - the same
        int d = (2 * rate + 16000) / 32000;
        if (d < 1) d = 1;
        float diff = s_fx_hist[0][(at - d) & 63];
        s_fx_hist[0][at] = clamp16(L - R);
        *l = clamp16(L + diff);
        *r = clamp16(R - diff);
        break;
    }
    case SOUND_OUTPUT_EFFECT_HEADPHONE: { // each side gets a quarter of the other, 24 samples before
        int d = (24 * rate + 16000) / 32000;
        if (d > 63) d = 63;
        float nl = L + s_fx_hist[1][(at - d) & 63], nr = R + s_fx_hist[0][(at - d) & 63];
        s_fx_hist[0][at] = nl / 4;
        s_fx_hist[1][at] = nr / 4;
        *l = clamp16(nl);
        *r = clamp16(nr);
        break;
    }
    case SOUND_OUTPUT_EFFECT_MONO:
        *l = *r = (L + R) / 2;
        break;
    default:
        break;
    }
}

void port_snd_render(int16_t *out, int frames, int rate) {
    snd_lock();
    s_output_rate = rate;
    double frame_samples = FRAME_SECONDS * rate;
    for (int n = 0; n < frames; n++) {
        if (s_frame_accum <= 0) {
            driver_frame();
            s_frame_accum += frame_samples;
        }
        s_frame_accum -= 1;
        float l = 0, r = 0;
        for (int i = 0; i < CHANNELS; i++) {
            Channel *c = &s_ch[i];
            if (!c->active) continue;
            int s;
            if (!channel_sample(c, &s)) {
                channel_stop(c);
                continue;
            }
            l += s * c->gain_l;
            r += s * c->gain_r;
            double before = c->pos;
            c->pos += c->step;
            if (c->type == CH_NOISE) {
                for (int k = (int) before; k < (int) c->pos; k++) {
                    int bit = c->lfsr & 1;
                    c->lfsr >>= 1;
                    if (bit) c->lfsr ^= 0x6000;
                }
            }
        }
        output_effect(&l, &r, rate);
        out[n * 2] = (int16_t) (l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[n * 2 + 1] = (int16_t) (r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    hapi_mutex_unlock(s_lock);
}

// ------------------------------------------------------------------------------------------------ alarms
typedef struct SndAlarm {
    OSAlarm os;
    uint32_t tick, period;
    int id;
    bool armed;
} SndAlarm;
static SndAlarm s_alarms[8];

static void alarm_fire(void *arg) {
    SndAlarm *a = arg;
    int no = (int) (a - s_alarms);
    if (FifoRecvCallbackTable[7]) {
        FifoRecvCallbackTable[7](7, (u32) (no | (a->id << 8)), 0);
    }
}

// ------------------------------------------------------------------------------------------------ commands
static void set_bytes(void *base, uint32_t offset, uint32_t data, int size) {
    uint8_t *p = (uint8_t *) base + offset;
    for (int i = 0; i < size; i++) p[i] = (uint8_t) (data >> (i * 8));
}

static void player_param(int p, uint32_t offset, uint32_t data, int size) {
    Player *pl = &s_players[p & 15];
    // Offsets of the ARM7 player record (SND_SetPlayer...): 4 priority, 6 volume (extFader), 0x1a tempo ratio.
    switch (offset) {
    case 4: pl->prio = (int) (data & 0xff); break;
    case 6: pl->ext_fader = (int16_t) data; break;
    case 0x1a: pl->tempo_ratio = (uint16_t) data; break;
    default: (void) size; break;
    }
}

static void track_param(int p, uint32_t mask, uint32_t offset, uint32_t data, int size) {
    Player *pl = &s_players[p & 15];
    for (int i = 0; i < 16; i++) {
        if (!(mask & (1u << i)) || pl->tracks[i] < 0) continue;
        Track *t = &s_tracks[pl->tracks[i]];
        // Offsets of the ARM7 track record: 9 pan, 0xa volume (extFader), 0xc pitch, 0x19 LFO speed.
        switch (offset) {
        case 9: t->ext_pan = (int8_t) data; break;
        case 0xa: t->ext_fader = (int16_t) data; break;
        case 0xc: t->ext_pitch = (int16_t) data; break;
        case 0x19: t->lfo.speed = (uint8_t) data; break;
        default: (void) size; break;
        }
    }
}

static void stop_range(uintptr_t start, uintptr_t end) {
    for (int p = 0; p < PLAYERS; p++) {
        Player *pl = &s_players[p];
        if (!pl->active && !pl->prepared) continue;
        bool hit = (uintptr_t) pl->bank >= start && (uintptr_t) pl->bank < end;
        for (int i = 0; i < 16 && !hit; i++) {
            if (pl->tracks[i] >= 0) {
                uintptr_t base = (uintptr_t) s_tracks[pl->tracks[i]].base;
                hit = base >= start && base < end;
            }
        }
        if (hit) player_stop(p, true);
    }
    for (int i = 0; i < CHANNELS; i++) {
        uintptr_t d = (uintptr_t) s_ch[i].data;
        if (s_ch[i].active && s_ch[i].type == CH_PCM && d >= start && d < end) channel_stop(&s_ch[i]);
    }
}

static void run_command(const SNDCommand *cmd) {
    const u32 *a = cmd->mArg;
    switch (cmd->mId) {
    case 0: // start sequence
    case 2: { // prepare: player, base, offset, bank
        if (s_debug) port_log("seq cmd %u player %u base %08x off %x bank %08x", cmd->mId, a[0], a[1], a[2], a[3]);
        int p = a[0] & 15;
        player_stop(p, true);
        Player *pl = &s_players[p];
        memset(pl->tracks, -1, sizeof(pl->tracks));
        pl->bank = (const uint8_t *) (uintptr_t) a[3];
        pl->tempo = 120;
        pl->tempo_ratio = 256;
        pl->tempo_counter = 240; // the first tick runs at once
        pl->volume = 127;
        pl->ext_fader = 0;
        pl->ticks = 0;
        pl->prio = 64;
        pl->paused = false;
        int ti = alloc_track();
        if (ti < 0) break;
        const uint8_t *base = (const uint8_t *) (uintptr_t) a[1];
        track_init(&s_tracks[ti], p, base, base + a[2]);
        pl->tracks[0] = (int8_t) ti;
        if (s_shared) {
            for (int i = 0; i < 16; i++) s_shared->mPlayer[p].mVariable[i] = -1;
            s_shared->mPlayer[p].mTickCounter = 0;
        }
        if (cmd->mId == 0) pl->active = true;
        else pl->prepared = true;
        break;
    }
    case 1: player_stop(a[0] & 15, false); break;
    case 3: if (s_players[a[0] & 15].prepared) s_players[a[0] & 15].active = true; break;
    case 4: s_players[a[0] & 15].paused = a[1] != 0; break;
    case 5: // skip ticks
        for (u32 i = 0; i < a[1] && s_players[a[0] & 15].active; i++) player_tick(a[0] & 15);
        break;
    case 6: player_param((int) a[0], a[1], a[2], (int) a[3]); break;
    case 7: track_param((int) (a[0] & 0xffffff), a[1], a[2], a[3], (int) (a[0] >> 24)); break;
    case 8: { // mute tracks: player, mask, mode
        Player *pl = &s_players[a[0] & 15];
        for (int i = 0; i < 16; i++) {
            if (!(a[1] & (1u << i)) || pl->tracks[i] < 0) continue;
            Track *t = &s_tracks[pl->tracks[i]];
            t->mute = a[2] != 0;
            if (a[2] >= 2) track_release_channels(t, a[2] == 3 ? 127 : -1);
        }
        break;
    }
    case 9: { // allocatable channels of tracks
        Player *pl = &s_players[a[0] & 15];
        for (int i = 0; i < 16; i++) {
            if ((a[1] & (1u << i)) && pl->tracks[i] >= 0) {
                s_tracks[pl->tracks[i]].channel_mask = (uint16_t) a[2];
                s_tracks[pl->tracks[i]].mask_set = true;
            }
        }
        break;
    }
    case 0xa: if (s_shared) s_shared->mPlayer[a[0] & 15].mVariable[a[1] & 15] = (s16) a[2]; break;
    case 0xb: if (s_shared) s_shared->mGlobalVariable[a[0] & 15] = (s16) a[1]; break;
    case 0xc: // start channels / alarms
        for (int i = 0; i < CHANNELS; i++) {
            if ((a[0] & (1u << i)) && s_ch[i].type != CH_FREE) {
                s_ch[i].active = true;
                update_channel_params(&s_ch[i]);
            }
        }
        for (int i = 0; i < 8; i++) {
            if (!(a[2] & (1u << i))) continue;
            SndAlarm *al = &s_alarms[i];
            OS_CancelAlarm(&al->os);
            OS_CreateAlarm(&al->os);
            if (al->period) {
                extern void OS_SetPeriodicAlarm(OSAlarm *, u64, u64, OSAlarmHandler, void *);
                OS_SetPeriodicAlarm(&al->os, OS_GetTick() + al->tick, al->period, alarm_fire, al);
            } else {
                OS_SetAlarm(&al->os, al->tick, alarm_fire, al);
            }
        }
        break;
    case 0xd: // stop channels / alarms
        for (int i = 0; i < CHANNELS; i++) {
            if (a[0] & (1u << i)) channel_stop(&s_ch[i]);
        }
        for (int i = 0; i < 8; i++) {
            if (a[2] & (1u << i)) OS_CancelAlarm(&s_alarms[i].os);
        }
        break;
    case 0xe: { // setup PCM channel
        int ch = a[0] & 15;
        Channel *c = &s_ch[ch];
        channel_stop(c);
        c->type = CH_PCM;
        c->sequenced = false;
        c->direct_timer = (uint16_t) (a[0] >> 16);
        c->data = (const uint8_t *) (uintptr_t) a[1];
        c->direct_volume = (a[2] >> 24) & 0x7f;
        c->direct_shift = (a[2] >> 22) & 3;
        uint32_t loop_len = a[2] & 0x3fffff, loop_start = a[3] & 0xffff;
        c->format = (a[3] >> 24) & 3;
        c->loop = ((a[3] >> 26) & 3) == 1;
        c->direct_pan = (a[3] >> 16) & 0x7f;
        int per_word = c->format == 0 ? 4 : c->format == 1 ? 2 : 8;
        c->loop_start = loop_start * per_word;
        c->loop_len = loop_len * per_word;
        if (c->format == 2) {
            c->adpcm_sample = (int16_t) rd16(c->data);
            c->adpcm_index = c->data[2] & 0x7f;
            c->adpcm_pos = 0;
            c->loop_saved = false;
            c->loop_start = loop_start ? (loop_start - 1) * 8 : 0;
        }
        if (!c->loop) c->loop_len = loop_len * per_word; // play to the end of loop_start + len
        c->pos = 0;
        c->track = -1;
        break;
    }
    case 0x12: { // setup alarm: number, first tick, period, id
        SndAlarm *al = &s_alarms[a[0] & 7];
        al->tick = a[1];
        al->period = a[2];
        al->id = (int) (a[3] & 0xff);
        break;
    }
    case 0x13: // channel timer
        for (int i = 0; i < CHANNELS; i++)
            if (a[0] & (1u << i)) s_ch[i].direct_timer = (uint16_t) a[1];
        break;
    case 0x14: // channel volume
        for (int i = 0; i < CHANNELS; i++)
            if (a[0] & (1u << i)) {
                s_ch[i].direct_volume = (int) a[1];
                s_ch[i].direct_shift = (int) a[2];
            }
        break;
    case 0x15: // channel pan
        for (int i = 0; i < CHANNELS; i++)
            if (a[0] & (1u << i)) s_ch[i].direct_pan = (int) a[1];
        break;
    case 0x16: s_surround_decay = (int) a[0]; break;
    case 0x17: s_master_volume = (int) a[0]; break;
    case 0x1a: s_locked_channels |= a[0]; break;
    case 0x1b: s_locked_channels &= ~a[0]; break;
    case 0x1c: // stop unlocked channels
        for (int i = 0; i < CHANNELS; i++)
            if ((a[0] & (1u << i)) && !(s_locked_channels & (1u << i))) channel_stop(&s_ch[i]);
        break;
    case SOUND_COMMAND_SET_SHARED_WORK:
        s_shared = (SNDSharedWork *) (uintptr_t) a[0];
        break;
    case 0x1e: case 0x1f: case 0x20: stop_range(a[0], a[1]); break;
    default: break;
    }
}

void port_snd_pxi_word(u32 data) {
    if (data == 0) {
        return; // "process the commands now": they already are
    }
    static bool inited;
    snd_lock();
    if (!inited) {
        inited = true;
        init_tables();
        for (int p = 0; p < PLAYERS; p++) memset(s_players[p].tracks, -1, sizeof(s_players[p].tracks));
        for (int i = 0; i < CHANNELS; i++) s_ch[i].track = -1;
    }
    for (const SNDCommand *cmd = (const SNDCommand *) (uintptr_t) data; cmd; cmd = cmd->mNext) {
        run_command(cmd);
    }
    if (s_shared) {
        // The status is current when the list is reported finished (the library checks both together).
        uint32_t status = 0;
        for (int p = 0; p < PLAYERS; p++) {
            if (s_players[p].active) status |= 1u << p;
        }
        s_shared->mPlayerStatus = status;
        s_shared->mFinishCommandTag++;
    }
    hapi_mutex_unlock(s_lock);
}

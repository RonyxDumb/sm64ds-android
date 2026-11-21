// Android entry points (com.mkds.port.Native).
#include "port.h"
#include "frame.h"
#include "guest_if.h"

#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifdef PORT_IPC
#include "ipc/ipc.h"
#endif

void android_render_set_window(ANativeWindow *window);
void android_render_set_layout(const int *rects);

// 32-bit: the DS address space must be reserved before anything else claims the low addresses: do it at load time.
// arm64: the game runs in its own process (ipc/ipc.h); this only creates the memory shared with it.
static bool s_memory_ok;
static char s_dir[512];

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void) vm, (void) reserved;
#ifdef PORT_IPC
    s_memory_ok = guest_load();
#else
    s_memory_ok = port_mem_init() && guest_load();
#endif
    return JNI_VERSION_1_6;
}

void android_crash_init(const char *dir, bool append);

JNIEXPORT void JNICALL Java_com_mkds_port_Native_init(JNIEnv *env, jclass cls, jstring dir) {
    (void) cls;
    const char *d = (*env)->GetStringUTFChars(env, dir, NULL);
    android_crash_init(d, false);
    snprintf(s_dir, sizeof(s_dir), "%s", d);
    (*env)->ReleaseStringUTFChars(env, dir, d);
    port_log("memory map %s", s_memory_ok ? "reserved" : "NOT reserved");
}

JNIEXPORT jstring JNICALL Java_com_mkds_port_Native_start(JNIEnv *env, jclass cls, jstring rom, jstring save) {
    (void) cls;
    if (!s_memory_ok) {
        return (*env)->NewStringUTF(env, "This device does not let the game reserve its memory at the addresses it "
                                         "needs (0x02000000-0x08000000).");
    }
    const char *rom_path = (*env)->GetStringUTFChars(env, rom, NULL);
    const char *save_path = (*env)->GetStringUTFChars(env, save, NULL);
#ifdef PORT_IPC
    // The game program sits next to this library (native libraries are extracted).
    char exe[600] = "libmkdsgame.so";
    Dl_info self;
    if (dladdr((void *) JNI_OnLoad, &self) && self.dli_fname && strrchr(self.dli_fname, '/')) {
        snprintf(exe, sizeof(exe), "%.*s/libmkdsgame.so", (int) (strrchr(self.dli_fname, '/') - self.dli_fname),
                 self.dli_fname);
    }
    const char *err = ipc_start(exe, rom_path, save_path, s_dir, 59826);
    (*env)->ReleaseStringUTFChars(env, rom, rom_path);
    (*env)->ReleaseStringUTFChars(env, save, save_path);
    if (err) {
        port_log("start: %s", err);
        return (*env)->NewStringUTF(env, err);
    }
    bool ok = true;
#else
    bool ok = port_rom_open(rom_path);
    if (ok) {
        port_save_set_path(save_path);
    }
    (*env)->ReleaseStringUTFChars(env, rom, rom_path);
    (*env)->ReleaseStringUTFChars(env, save, save_path);
    if (!ok) {
        return (*env)->NewStringUTF(env, "The ROM could not be opened.");
    }
    guest_set_vblank_rate(59826);
    guest_start_game();
#endif
    extern void android_audio_start(void);
    android_audio_start();
    return NULL;
}

JNIEXPORT void JNICALL Java_com_mkds_port_Native_setSurface(JNIEnv *env, jclass cls, jobject surface) {
    (void) cls;
    android_render_set_window(surface ? ANativeWindow_fromSurface(env, surface) : NULL);
}

JNIEXPORT void JNICALL Java_com_mkds_port_Native_setLayout(JNIEnv *env, jclass cls, jint tx, jint ty, jint tw, jint th,
                                                           jint bx, jint by, jint bw, jint bh) {
    (void) env, (void) cls;
    int rects[8] = {tx, ty, tw, th, bx, by, bw, bh};
    android_render_set_layout(rects);
}

void android_set_input(int buttons, bool touching, int x, int y) {
    static PortInput *in; // in memory the guest can address (arm64 in-process bridge)
    if (!in) {
        extern void *host_low_alloc(size_t size);
        in = host_low_alloc(sizeof(*in));
    }
    *in = (PortInput) {(uint16_t) buttons, touching, (uint16_t) x, (uint16_t) y, false};
    guest_input_set(in);
}

JNIEXPORT void JNICALL Java_com_mkds_port_Native_setInput(JNIEnv *env, jclass cls, jint buttons, jboolean touching,
                                                          jint x, jint y) {
    (void) env, (void) cls;
    android_set_input(buttons, touching, x, y);
}

JNIEXPORT void JNICALL Java_com_mkds_port_Native_setPaused(JNIEnv *env, jclass cls, jboolean paused) {
    (void) env, (void) cls;
    guest_set_paused(paused);
    extern void android_audio_pause(bool paused);
    android_audio_pause(paused);
    if (paused) {
        guest_save_flush();
    }
}

// Widescreen: the race view's aspect ratio (the whole display); applies from the next race.
JNIEXPORT void JNICALL Java_com_mkds_port_Native_setWideAspect(JNIEnv *env, jclass cls, jfloat aspect) {
    (void) env, (void) cls;
    extern void present_set_wide(float aspect);
    present_set_wide(aspect);
    guest_set_aspect((int32_t) (aspect * 4096.0f + 0.5f));
}

// The console language for the game (before start): 1 English, 2 French, 3 German, 4 Italian, 5 Spanish.
JNIEXPORT void JNICALL Java_com_mkds_port_Native_setLanguage(JNIEnv *env, jclass cls, jint language) {
    (void) env, (void) cls;
    guest_set_language(language);
}

// The player's name for multiplayer and races (empty: the nickname of the save file). The game's font has the
// Latin characters; anything else becomes '?'.
JNIEXPORT void JNICALL Java_com_mkds_port_Native_setPlayerName(JNIEnv *env, jclass cls, jstring name) {
    (void) cls;
    uint16_t out[PORT_PLAYER_NAME] = {0};
    const jchar *chars = (*env)->GetStringChars(env, name, NULL);
    jsize length = (*env)->GetStringLength(env, name);
    for (jsize i = 0; chars && i < length && i < 10; i++) {
        out[i] = chars[i] >= 0x20 && chars[i] <= 0xff && chars[i] != 0x7f ? chars[i] : '?';
    }
    if (chars) (*env)->ReleaseStringChars(env, name, chars);
    guest_set_player_name(out);
}

// The game's volume in percent (the voice chat is not affected).
JNIEXPORT void JNICALL Java_com_mkds_port_Native_setGameVolume(JNIEnv *env, jclass cls, jint percent) {
    (void) env, (void) cls;
    extern void android_audio_set_volume(int percent);
    android_audio_set_volume(percent);
}

// Whether the game shows a race (the activity picks the screen layout from it).
JNIEXPORT jboolean JNICALL Java_com_mkds_port_Native_isRace(JNIEnv *env, jclass cls) {
    (void) env, (void) cls;
    extern bool present_is_race(void);
    return present_is_race();
}

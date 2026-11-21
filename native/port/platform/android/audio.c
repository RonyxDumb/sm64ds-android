// Audio output: an AAudio stream whose callback runs the sound driver and mixes straight into the device buffer.
#include "port.h"

#include <aaudio/AAudio.h>
#include <pthread.h>

#include "guest_if.h"

static AAudioStream *s_stream;
static int s_rate = 48000;
static volatile int s_volume = 256; // the game's volume, 0..256 (the network settings: lower, to hear the voice chat)

void android_audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent * 256 / 100;
}

static aaudio_data_callback_result_t callback(AAudioStream *stream, void *user, void *data, int32_t frames) {
    (void) stream, (void) user;
    guest_snd_render(data, frames, s_rate);
    int volume = s_volume;
    if (volume < 256) {
        int16_t *samples = data;
        for (int32_t i = 0; i < frames * 2; i++) samples[i] = (int16_t) ((samples[i] * volume) >> 8);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void error_callback(AAudioStream *stream, void *user, aaudio_result_t error);

static void open_stream(void) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) {
        port_log("audio: no AAudio");
        return;
    }
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, callback, NULL);
    AAudioStreamBuilder_setErrorCallback(b, error_callback, NULL);
    if (AAudioStreamBuilder_openStream(b, &s_stream) != AAUDIO_OK) {
        port_log("audio: cannot open the output stream");
        s_stream = NULL;
    } else {
        s_rate = AAudioStream_getSampleRate(s_stream);
        AAudioStream_setBufferSizeInFrames(s_stream, AAudioStream_getFramesPerBurst(s_stream) * 2);
        AAudioStream_requestStart(s_stream);
        port_log("audio: %d Hz, burst %d frames", s_rate, AAudioStream_getFramesPerBurst(s_stream));
    }
    AAudioStreamBuilder_delete(b);
}

// The device changed (headphones, Bluetooth): reopen on another thread, as AAudio requires.
static void *reopen(void *arg) {
    (void) arg;
    if (s_stream) {
        AAudioStream_close(s_stream);
        s_stream = NULL;
    }
    open_stream();
    return NULL;
}

static void error_callback(AAudioStream *stream, void *user, aaudio_result_t error) {
    (void) stream, (void) user;
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        pthread_t t;
        pthread_create(&t, NULL, reopen, NULL);
        pthread_detach(t);
    }
}

void android_audio_start(void) {
    if (!s_stream) open_stream();
}

void android_audio_pause(bool paused) {
    if (!s_stream) return;
    if (paused) AAudioStream_requestPause(s_stream);
    else AAudioStream_requestStart(s_stream);
}

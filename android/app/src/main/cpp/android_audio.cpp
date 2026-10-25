#include "android_audio.h"
namespace sm64ds {
aaudio_data_callback_result_t AndroidAudio::callback(AAudioStream *stream, void *user, void *data,
                                                     int32_t frames) {
    auto &self = *static_cast<AndroidAudio *>(user);
    self.mixer_.render(static_cast<float *>(data), frames, AAudioStream_getSampleRate(stream));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void AndroidAudio::error(AAudioStream *, void *user, aaudio_result_t) {
    static_cast<AndroidAudio *>(user)->disconnected_.store(true, std::memory_order_relaxed);
}
bool AndroidAudio::start() {
    if (stream_ && !disconnected())
        return true;
    close();
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
        return false;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setDataCallback(builder, callback, this);
    AAudioStreamBuilder_setErrorCallback(builder, error, this);
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream_);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        stream_ = nullptr;
        return false;
    }
    if (AAudioStream_getFormat(stream_) != AAUDIO_FORMAT_PCM_FLOAT ||
        AAudioStream_getChannelCount(stream_) != 2) {
        close();
        return false;
    }
    AAudioStream_setBufferSizeInFrames(stream_, AAudioStream_getFramesPerBurst(stream_) * 2);
    disconnected_.store(false, std::memory_order_relaxed);
    if (AAudioStream_requestStart(stream_) != AAUDIO_OK) {
        close();
        return false;
    }
    return true;
}
void AndroidAudio::close() {
    if (stream_) {
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
    mixer_.reset();
}
}

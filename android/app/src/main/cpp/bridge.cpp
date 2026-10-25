#include <jni.h>
#include <exception>
#include <mutex>
#include "rom_probe.h"
#include "session.h"
#include <memory>
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_ronyxdumb_sm64ds_NativeBridge_probe(JNIEnv *env, jclass, jstring path) {
    if (!path)
        return nullptr;
    const char *chars = env->GetStringUTFChars(path, nullptr);
    if (!chars)
        return nullptr;
    std::string result, error;
    try {
        std::lock_guard<std::mutex> guard(sm64ds::platform_mutex());
        result = sm64ds::probe_rom(chars);
    } catch (const std::exception &e) {
        error = e.what();
    }
    env->ReleaseStringUTFChars(path, chars);
    if (!error.empty()) {
        env->ThrowNew(env->FindClass("java/io/IOException"), error.c_str());
        return nullptr;
    }
    return env->NewStringUTF(result.c_str());
}

// Session handles are confined to the GLSurfaceView render thread. Lifecycle
// teardown is queued there before GLSurfaceView pauses (no UI-thread deletion).
extern "C" JNIEXPORT jlong JNICALL
Java_io_github_ronyxdumb_sm64ds_NativeBridge_createRenderer(JNIEnv *env, jclass, jstring path) {
    if (!path)
        return 0;
    const char *chars = env->GetStringUTFChars(path, nullptr);
    if (!chars)
        return 0;
    std::unique_ptr<sm64ds::Session> session;
    std::string error;
    try {
        session = std::make_unique<sm64ds::Session>(chars);
        session->surface();
    } catch (const std::exception &e) {
        error = e.what();
        if (session)
            session->release();
    }
    env->ReleaseStringUTFChars(path, chars);
    if (!error.empty()) {
        env->ThrowNew(env->FindClass("java/io/IOException"), error.c_str());
        return 0;
    }
    return reinterpret_cast<jlong>(session.release());
}
extern "C" JNIEXPORT void JNICALL Java_io_github_ronyxdumb_sm64ds_NativeBridge_drawRenderer(
    JNIEnv *, jclass, jlong handle, jint width, jint height, jfloat dt, jint buttons, jfloat x,
    jfloat y, jint tx, jint ty, jboolean audioEnabled) {
    if (handle)
        reinterpret_cast<sm64ds::Session *>(handle)->frame(
            width, height, dt, static_cast<uint32_t>(buttons), x, y, tx, ty, audioEnabled);
}
extern "C" JNIEXPORT void JNICALL Java_io_github_ronyxdumb_sm64ds_NativeBridge_destroyRenderer(
    JNIEnv *, jclass, jlong handle, jboolean contextValid) {
    if (handle) {
        auto *session = reinterpret_cast<sm64ds::Session *>(handle);
        session->release(contextValid);
        delete session;
    }
}

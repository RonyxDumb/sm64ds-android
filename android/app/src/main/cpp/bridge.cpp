#include <jni.h>
#include <exception>
#include <mutex>
#include "rom_probe.h"
#include "crash_diagnostics.h"
#include "session.h"
#include <memory>
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_ronyxdumb_sm64ds_NativeBridge_configureCrashLog(
    JNIEnv *env, jclass, jstring path) {
    if (!path) return JNI_FALSE;
    const char *file = env->GetStringUTFChars(path, nullptr);
    if (!file) return JNI_FALSE;
    const bool installed = sm64ds::diagnostics::install(file);
    env->ReleaseStringUTFChars(path, file);
    return installed ? JNI_TRUE : JNI_FALSE;
}

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
        sm64ds::diagnostics::note("NATIVE-EXCEPTION", error.c_str());
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
    JNIEnv *env, jclass, jlong handle, jint width, jint height, jfloat dt, jint buttons, jfloat x,
    jfloat y, jint tx, jint ty, jboolean audioEnabled) {
    try {
        if (handle)
            reinterpret_cast<sm64ds::Session *>(handle)->frame(
                width, height, dt, static_cast<uint32_t>(buttons), x, y, tx, ty, audioEnabled);
    } catch (const std::exception &e) {
        sm64ds::diagnostics::note("NATIVE-RENDER", e.what());
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), e.what());
    } catch (...) {
        sm64ds::diagnostics::note("NATIVE-RENDER", "Unknown non-standard native exception");
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
                      "Unknown native exception in render loop");
    }
}
extern "C" JNIEXPORT void JNICALL Java_io_github_ronyxdumb_sm64ds_NativeBridge_destroyRenderer(
    JNIEnv *, jclass, jlong handle, jboolean contextValid) {
    if (handle) {
        auto *session = reinterpret_cast<sm64ds::Session *>(handle);
        try {
            session->release(contextValid);
        } catch (const std::exception &e) {
            sm64ds::diagnostics::note("NATIVE-RELEASE", e.what());
        } catch (...) {
            sm64ds::diagnostics::note("NATIVE-RELEASE", "Unknown exception");
        }
        delete session;
    }
}

#include <jni.h>
#include <exception>
#include <mutex>
#include "rom_probe.h"
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_ronyxdumb_sm64ds_NativeBridge_probe(JNIEnv *env, jclass, jstring path) {
    if (!path) return nullptr;
    const char *chars = env->GetStringUTFChars(path, nullptr);
    if (!chars) return nullptr;
    std::string result, error;
    try {
        static std::mutex lock;
        std::lock_guard<std::mutex> guard(lock);
        result = sm64ds::probe_rom(chars);
    } catch (const std::exception &e) { error = e.what(); }
    env->ReleaseStringUTFChars(path, chars);
    if (!error.empty()) {
        env->ThrowNew(env->FindClass("java/io/IOException"), error.c_str());
        return nullptr;
    }
    return env->NewStringUTF(result.c_str());
}

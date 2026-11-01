#include <jni.h>

#include <new>

#include <android/log.h>

#include "audio/AudioEngine.h"

#define LOG_TAG "KukosinAudioJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Protocolo entero compartido con com.kukosin.app.audio.AudioEngine.
//
// Formato:      0 = PCM I16, 1 = PCM I24, 2 = PCM Float 32, 3 = PCM I32.
// Estado:       valores de oboe::StreamState.
// Info[i]:      0 api · 1 formato stream · 2 rate stream · 3 canales stream ·
//               4 framesPerBurst · 5 bufferFrames · 6 deviceId · 7 xRuns ·
//               8 exclusivo · 9 bitPerfect · 10 passthrough · 11 errorCode ·
//               12 volumen % · 13 formato origen · 14 rate origen · 15 canales origen

namespace {

audio::AudioEngine* engineFrom(jlong handle) {
    return reinterpret_cast<audio::AudioEngine*>(handle);
}

audio::PcmFormat toFormat(jint code) {
    switch (code) {
        case 0: return audio::PcmFormat::kI16;
        case 1: return audio::PcmFormat::kI24;
        case 2: return audio::PcmFormat::kF32;
        case 3: return audio::PcmFormat::kI32;
        default: return audio::PcmFormat::kI16;
    }
}

constexpr jint kInfoSize = 16;

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeCreate(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* engine = new (std::nothrow) audio::AudioEngine();
    if (engine == nullptr) {
        LOGE("sin memoria para AudioEngine");
        return 0;
    }
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeDelete(JNIEnv* /*env*/, jobject /*thiz*/,
                                                    jlong handle) {
    delete engineFrom(handle);
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeConfigure(JNIEnv* /*env*/, jobject /*thiz*/,
                                                       jlong handle, jint format,
                                                       jint sampleRate, jint channelCount,
                                                       jint deviceId, jboolean strict,
                                                       jboolean requireExclusive, jint bufferMs,
                                                       jboolean fades) {
    auto* engine = engineFrom(handle);
    if (engine == nullptr) {
        return static_cast<jint>(oboe::Result::ErrorNull);
    }
    audio::EngineConfig config;
    config.sourceFormat = toFormat(format);
    config.sourceSampleRate = sampleRate > 0 ? sampleRate : 0;
    config.sourceChannels = channelCount > 0 ? channelCount : 2;
    config.deviceId = deviceId;
    config.strictBitPerfect = strict == JNI_TRUE;
    config.requireExclusive = requireExclusive == JNI_TRUE;
    config.bufferMs = bufferMs > 0 ? bufferMs : 250;
    config.fades = fades == JNI_TRUE;
    return static_cast<jint>(engine->open(config));
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeStart(JNIEnv* /*env*/, jobject /*thiz*/,
                                                   jlong handle) {
    auto* engine = engineFrom(handle);
    return engine == nullptr ? static_cast<jint>(oboe::Result::ErrorNull)
                             : static_cast<jint>(engine->start());
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/,
                                                  jlong handle) {
    auto* engine = engineFrom(handle);
    return engine == nullptr ? static_cast<jint>(oboe::Result::ErrorNull)
                             : static_cast<jint>(engine->stop());
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativePause(JNIEnv* /*env*/, jobject /*thiz*/,
                                                   jlong handle) {
    auto* engine = engineFrom(handle);
    return engine == nullptr ? static_cast<jint>(oboe::Result::ErrorNull)
                             : static_cast<jint>(engine->pause());
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeFlush(JNIEnv* /*env*/, jobject /*thiz*/,
                                                   jlong handle) {
    auto* engine = engineFrom(handle);
    return engine == nullptr ? static_cast<jint>(oboe::Result::ErrorNull)
                             : static_cast<jint>(engine->flush());
}

/**
 * Copia PCM ya decodificado al ring del motor.
 * Devuelve los bytes de ORIGEN aceptados (0 = reintentar más tarde).
 */
JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeWritePacked(JNIEnv* env, jobject /*thiz*/,
                                                         jlong handle, jobject buffer,
                                                         jint offset, jint length) {
    auto* engine = engineFrom(handle);
    if (engine == nullptr || buffer == nullptr || length <= 0 || offset < 0) {
        return 0;
    }
    void* base = env->GetDirectBufferAddress(buffer);
    if (base == nullptr) {
        return 0;
    }
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (capacity < 0 || static_cast<jlong>(offset) + static_cast<jlong>(length) > capacity) {
        return 0;
    }
    const size_t accepted = engine->writePacked(
            static_cast<const uint8_t*>(base) + offset, static_cast<size_t>(length));
    return static_cast<jint>(accepted);
}

JNIEXPORT void JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeSetVolume(JNIEnv* /*env*/, jobject /*thiz*/,
                                                       jlong handle, jfloat volume) {
    auto* engine = engineFrom(handle);
    if (engine != nullptr) {
        engine->setVolume(volume);
    }
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeGetState(JNIEnv* /*env*/, jobject /*thiz*/,
                                                      jlong handle) {
    auto* engine = engineFrom(handle);
    if (engine == nullptr) {
        return static_cast<jint>(oboe::StreamState::Closed);
    }
    return static_cast<jint>(engine->state());
}

JNIEXPORT jlong JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeGetSourceFrames(JNIEnv* /*env*/, jobject /*thiz*/,
                                                             jlong handle) {
    auto* engine = engineFrom(handle);
    if (engine == nullptr) {
        return 0;
    }
    return static_cast<jlong>(engine->sourceFramesConsumed());
}

JNIEXPORT jint JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeErrorCode(JNIEnv* /*env*/, jobject /*thiz*/,
                                                       jlong handle) {
    auto* engine = engineFrom(handle);
    if (engine == nullptr) {
        return static_cast<jint>(oboe::Result::ErrorNull);
    }
    return static_cast<jint>(engine->lastErrorCode());
}

JNIEXPORT void JNICALL
Java_com_kukosin_app_audio_AudioEngine_nativeGetInfo(JNIEnv* env, jobject /*thiz*/,
                                                     jlong handle, jintArray out) {
    auto* engine = engineFrom(handle);
    if (out == nullptr) return;
    const jsize size = env->GetArrayLength(out);
    if (size < kInfoSize) return;

    jint values[kInfoSize] = {-1, -1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 100, -1, 0, 0};
    if (engine != nullptr) {
        const audio::StreamInfo info = engine->info();
        values[0] = info.api;
        values[1] = info.format;
        values[2] = info.sampleRate;
        values[3] = info.channels;
        values[4] = info.framesPerBurst;
        values[5] = info.bufferFrames;
        values[6] = info.deviceId;
        values[7] = info.xRuns;
        values[8] = info.exclusive ? 1 : 0;
        values[9] = info.bitPerfect ? 1 : 0;
        values[10] = info.passthrough ? 1 : 0;
        values[11] = info.errorCode;
        values[12] = info.volumePercent;
        values[13] = info.sourceFormat;
        values[14] = info.sourceSampleRate;
        values[15] = info.sourceChannels;
    }
    env->SetIntArrayRegion(out, 0, kInfoSize, values);
}

} // extern "C"

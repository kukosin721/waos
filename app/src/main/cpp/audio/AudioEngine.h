#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <oboe/Oboe.h>

namespace audio {

/** Formato PCM. Los códigos son parte del protocolo JNI: no reordenar. */
enum class PcmFormat : int32_t {
    kI16 = 0,
    kI24 = 1,
    kF32 = 2,
    kI32 = 3,
};

int32_t bytesPerSample(PcmFormat format);
const char* formatName(PcmFormat format);

/** Lo que pide la app: el formato REAL de la pista decodificada. */
struct EngineConfig {
    PcmFormat sourceFormat = PcmFormat::kI16;
    int32_t sourceSampleRate = 0;   // 0 = no definido
    int32_t sourceChannels = 2;
    int32_t deviceId = 0;           // 0 = dispositivo por defecto
    bool strictBitPerfect = true;   // si true, nunca se acepta conversión
    bool requireExclusive = false;  // si true, no se acepta un stream compartido
    int32_t bufferMs = 250;         // objetivo de buffer (estabilidad)
    bool fades = true;              // fundido de entrada/salida (evita clics)
};

/** Lo que devolvió el hardware. La UI muestra esto tal cual. */
struct StreamInfo {
    int32_t api = -1;           // 0 = AAudio, 1 = OpenSL ES
    int32_t format = -1;        // PcmFormat real del stream
    int32_t sampleRate = 0;
    int32_t channels = 0;
    int32_t sourceFormat = -1;  // formato de la pista
    int32_t sourceSampleRate = 0;
    int32_t sourceChannels = 0;
    int32_t volumePercent = 100;
    int32_t framesPerBurst = 0;
    int32_t bufferFrames = 0;
    int32_t deviceId = 0;
    int32_t xRuns = -1;         // -1 = no reportado por la API
    int32_t errorCode = 0;      // último error nativo (0 = ninguno)
    bool exclusive = false;
    bool bitPerfect = false;    // exclusivo + mismo formato + mismo rate
    bool passthrough = false;   // el write path es memcpy
};

/** Ring lock-free SPSC de bytes PCM ya en el formato del stream. */
class ByteRingBuffer {
public:
    ByteRingBuffer();
    ByteRingBuffer(const ByteRingBuffer&) = delete;
    ByteRingBuffer& operator=(const ByteRingBuffer&) = delete;

    void resize(size_t capacity);   // sólo sin stream abierto
    size_t write(const uint8_t* data, size_t count);
    size_t read(uint8_t* out, size_t count);
    void clear();
    size_t availableToRead() const;
    size_t freeSpace() const;
    size_t capacity() const { return capacity_; }

private:
    size_t capacity_ = 0;
    size_t mask_ = 0;
    std::vector<uint8_t> buffer_;
    std::atomic<size_t> readIndex_{0};
    std::atomic<size_t> writeIndex_{0};
};

/**
 * FIR polifásico con ventana de Kaiser para el caso (raro) en que el DAC no
 * acepta el rate de la pista. Misma matemática que la versión Kotlin anterior,
 * pero del lado del productor: la conversión nunca ocurre en el callback.
 */
class PolyphaseResampler {
public:
    void configure(int32_t inRate, int32_t outRate, int32_t channels);
    void reset();
    bool bypass() const { return step_ == 1.0; }
    double step() const { return step_; }
    int32_t halfFrames() const { return half_; }
    /** Devuelve frames escritos en `out`. */
    int32_t process(const float* in, int32_t inFrames, float* out, int32_t maxOutFrames);

private:
    static double besselI0(double x);
    void rebuildKernel();

    int32_t channels_ = 2;
    int32_t half_ = 12;
    int32_t taps_ = 24;
    double step_ = 1.0;
    double pos_ = 0.0;
    int32_t historyFrames_ = 0;
    std::vector<float> kernel_;
    std::vector<float> work_;
};

/**
 * Motor de salida. Un único stream de Oboe abierto EXACTAMENTE con el formato de
 * la pista cuando es posible (bit-perfect). El callback sólo copia bytes: nunca
 * convierte, nunca aplica ganancia, nunca mezcla.
 */
class AudioEngine final : public oboe::AudioStreamDataCallback,
                          public oboe::AudioStreamErrorCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    oboe::Result open(const EngineConfig& config);
    oboe::Result close();

    oboe::Result start();
    oboe::Result stop();
    oboe::Result pause();
    oboe::Result flush();

    void setVolume(float volume);

    /**
     * Peticiones del fundido. Se resuelven en el callback (donde está el tiempo real),
     * nunca en el hilo que las pide:
     *  0 = nada · 1 = fundido de entrada · 2 = fundido de salida + parada · 3 = re-arme.
     */
    enum FadeRequest : int32_t {
        kFadeNone = 0,
        kFadeIn = 1,
        kFadeOutAndStop = 2,
        kFadeRearm = 3,
    };

    /** Bytes de origen aceptados (0 = reintentar más tarde). */
    size_t writePacked(const uint8_t* data, size_t byteCount);

    StreamInfo info() const;
    int64_t sourceFramesConsumed() const;
    oboe::StreamState state() const;

    /** Último error nativo (0 = ninguno). Lo usa el sink para fallar en vez de colgarse. */
    int32_t lastErrorCode() const { return errorCode_.load(std::memory_order_relaxed); }
    bool isOpen() const { return stream_ != nullptr; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                          void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    oboe::Result openStream(PcmFormat format,
                            int32_t sampleRate,
                            int32_t channels,
                            oboe::SharingMode sharing,
                            oboe::PerformanceMode performance);
    void finalizeOpen(const EngineConfig& config);
    oboe::Result setStreamState(int32_t target);

    size_t writeToRing(const uint8_t* data, size_t byteCount);
    size_t writeConverted(const uint8_t* data, size_t byteCount);

    /** Rampa lineal sobre los frames ya copiados al buffer de salida. */
    void applyGainRamp(uint8_t* data, int32_t frames);

    /** Arranca el stream recién cuando hay datos que suenan (evita el silencio inicial). */
    oboe::Result maybeStart();

    std::shared_ptr<oboe::AudioStream> stream_;

    // Diagnóstico
    int32_t apiCode_ = -1;
    int32_t streamFormat_ = -1;
    int32_t streamRate_ = 0;
    int32_t streamChannels_ = 0;
    int32_t streamFrameBytes_ = 4;
    int32_t deviceId_ = 0;
    bool exclusive_ = false;
    std::atomic<int32_t> errorCode_{0};
    std::atomic<int64_t> underruns_{0};
    std::atomic<int64_t> streamFramesConsumed_{0};
    int32_t hardwareBufferFrames_ = 0;   // lo que realmente usa AAudio por debajo

    // Formato de origen
    PcmFormat sourceFormat_ = PcmFormat::kI16;
    int32_t sourceRate_ = 0;
    int32_t sourceChannels_ = 2;

    bool passthrough_ = true;
    std::atomic<float> volume_{1.0f};
    float volumeApplied_ = 1.0f;

    // Fundidos: lo escribe el hilo que pide (play/pause/flush), lo resuelve el callback.
    std::atomic<int32_t> fadeRequest_{kFadeNone};
    std::atomic<bool> holding_{false};      // silencio sin consumir (en pausa)
    bool fades_ = true;
    int32_t fadeInFrames_ = 0;              // 25 ms al rate del stream
    int32_t fadeOutFrames_ = 0;             // 60 ms
    int32_t rearmFrames_ = 0;               // 12 ms (saltos: que se sienta instantáneo)
    int32_t prerollFrames_ = 0;             // datos mínimos antes de arrancar el stream
    std::atomic<bool> startPending_{false}; // play pedido, esperando preroll
    // Estado del callback (sólo lo toca el callback).
    float gain_ = 1.0f;
    float gainStep_ = 0.0f;
    bool stopAfterFade_ = false;

    ByteRingBuffer ring_;

    // Buffers del productor (sólo los toca el hilo que llama writePacked).
    std::vector<float> srcFloats_;
    std::vector<float> resampled_;
    std::vector<float> mapped_;
    std::vector<uint8_t> outBytes_;
    std::vector<uint8_t> pending_;
    PolyphaseResampler resampler_;
};

} // namespace audio

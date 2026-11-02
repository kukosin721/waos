#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <oboe/Oboe.h>

#if defined(__has_include)
#  if __has_include(<oboe/OboeExtensions.h>)
#    include <oboe/OboeExtensions.h>
#    define AUDIOENGINE_HAVE_OBOE_EXTENSIONS 1
#  endif
#endif

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
    /**
     * Si true, el motor NO cae al dispositivo por defecto cuando `deviceId` no se
     * puede abrir. Es obligatorio para los modos que apuntan a un DAC USB: preferimos
     * fallar y que la UI lo muestre antes que mandar la señal a otro altavoz.
     */
    bool requireDevice = false;
    int32_t bufferMs = 250;         // objetivo de buffer (estabilidad)
    bool fades = true;              // fundido de entrada/salida (evita clics)
    /** Preroll del arranque en ms (-1 = automático). Más chico = arranca antes. */
    int32_t prerollMs = -1;
    /** 0 = volumen lineal, 1 = perceptual (v^2,5 en las muestras). */
    int32_t volumeCurve = 0;
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
    int32_t latencyMicros = -1; // latencia real medida (timestamp de AAudio)
    bool mmap = false;          // el stream usa el camino MMAP (sin mezclador)
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
 * FIR polifásico con ventana de Kaiser para el caso en que el DAC no acepta el
 * rate de la pista. Corre en el hilo productor: el callback nunca convierte.
 *
 * `process()` es incremental: lo que no entre en `maxOutFrames` se conserva en la
 * ventana de historia, así el productor puede respetar el espacio libre del ring
 * sin perder muestras ni bloquear.
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
 * Motor de salida sobre AAudio (Oboe).
 *
 * Contrato con Media3 (`AudioOutput`):
 *
 *  - `writePacked()` devuelve cuántos bytes de ORIGEN aceptó. Si el ring está
 *    lleno devuelve 0 y Media3 reintenta el mismo buffer: nunca se pierde audio
 *    ni se encola de más.
 *  - `stop()` significa "deja sonar lo que queda y para" (`playToEndOfStream()`
 *    de `DefaultAudioSink` llama `stop()`). No se descarta nada.
 *  - `flush()` descarta: pausa → espera estado PAUSED → `requestFlush()` →
 *    espera FLUSHED. AAudio sólo acepta flush en OPEN/PAUSED/STOPPED/FLUSHED,
 *    así que hacerlo desde STARTED no hace nada (antes se intentaba así).
 *  - `positionMicros()` es la posición de PLAYOUT (timestamp de AAudio menos la
 *    latencia real), no los frames que consumió el callback.
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
    oboe::Result stop();     // fin de stream: drena y se detiene solo
    oboe::Result pause();    // fundido corto y se detiene (los datos quedan)
    oboe::Result flush();    // descarta lo pendiente (seek / cambio de pista)
    /**
     * Como `flush()`, pero para el hueco entre dos pistas: descarta el audio propio de
     * inmediato y hace las transiciones de estado del HAL de forma **no bloqueante**.
     *
     * `flush()` espera a que AAudio pase por PAUSED (hasta 120 ms) y después por FLUSHED
     * (otros 120 ms). En un seek suelto no se nota, pero entre canción y canción ese
     * bloqueo se sumaba al arranque de la siguiente y se escuchaba como un bache.
     */
    oboe::Result flushForTransition();

    /**
     * Curva de volumen: 0 = lineal (la de siempre), 1 = perceptual (v^2,5 en las muestras).
     * Con la curva perceptual el deslizador se siente parejo en todo su recorrido en vez de
     * saltar desde el 20 %; el bit-perfect no cambia porque al 100 % la ganancia es 1.
     */
    void setVolumeCurve(int32_t curve);

    /** Espera a que el DAC consuma lo encolado. Devuelve true si drenó. */
    bool drain(int32_t timeoutMs);

    void setVolume(float volume);
    void setFadesEnabled(bool enabled);

    /** Bytes de origen aceptados (0 = reintentar más tarde). */
    size_t writePacked(const uint8_t* data, size_t byteCount);

    StreamInfo info() const;

    /** Posición de playout en frames del stream (monótona dentro del stream). */
    int64_t positionFrames();
    /** Posición de playout en microsegundos (dominio temporal de la pista). */
    int64_t positionMicros();
    /** Latencia estimada en microsegundos (-1 = desconocida). */
    int64_t latencyMicros();
    /** Frames de origen encolados desde el último flush/close. */
    int64_t sourceFramesQueued() const;
    /** Frames de stream consumidos por el callback desde el último flush/close. */
    int64_t framesConsumed() const { return framesConsumed_.load(std::memory_order_relaxed); }
    /** Frames de stream encolados desde el último flush/close. */
    int64_t framesQueued() const { return framesQueued_.load(std::memory_order_relaxed); }
    /** true si el callback ya agotó todo lo encolado (fin de stream real). */
    bool endOfStreamReached() const;

    oboe::StreamState state() const;

    /**
     * Espera (con timeout) a que termine la apertura diferida lanzada por `open()`.
     * Devuelve false si falló al abrir.
     */
    bool awaitOpen(int32_t timeoutMs = 2000);

    /** Último error nativo (0 = ninguno). Lo usa el sink para fallar en vez de colgarse. */
    int32_t lastErrorCode() const { return errorCode_.load(std::memory_order_relaxed); }
    bool isOpen() const { return stream_ != nullptr; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                          void* audioData,
                                          int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    enum FadeRequest : int32_t {
        kFadeNone = 0,
        kFadeIn = 1,
        kFadeOutAndHold = 2,
        kFadeRearm = 3,
    };

    /** Cuerpo de close() sin tomar el mutex (open() ya lo tiene tomado). */
    void closeLocked();

    /** Hilo de apertura: abre el stream fuera del hilo que llama a start()/write(). */
    void openThreadMain();
    void joinOpenThread();

    oboe::Result openStream(PcmFormat format,
                            int32_t sampleRate,
                            int32_t channels,
                            oboe::SharingMode sharing,
                            oboe::PerformanceMode performance);
    oboe::Result tryOpenCandidates(const EngineConfig& config);
    void finalizeOpen(const EngineConfig& config);
    oboe::Result flushInternal(bool waitForHardware);
    /** Ganancia que corresponde al volumen actual, con la curva elegida. */
    float targetGain() const;
    oboe::Result setStreamState(int32_t target);
    /** Espera (con timeout) a que el stream llegue a alguno de los estados dados. */
    bool waitForState(std::initializer_list<oboe::StreamState> states, int32_t timeoutMs);

    void resetStreamAccounting();
    void updatePlayoutPosition(bool forceZeroLatency);

    size_t writeToRing(const uint8_t* data, size_t byteCount);

    /** Rampa lineal sobre los frames ya copiados al buffer de salida. */
    void applyGainRamp(uint8_t* data, int32_t frames);

    oboe::Result maybeStart();

    std::shared_ptr<oboe::AudioStream> stream_;
    mutable std::mutex controlMutex_;

    // Apertura diferida: `AudioEngine::open()` guarda la configuración y devuelve; un
    // hilo aparte abre el stream de AAudio (puede tardar decenas de ms cuando el HAL
    // negocia exclusividad). Sin esto, el primer `write()` de Media3 pagaba la apertura
    // dentro del hilo de audio y la reproducción "arrancaba trabada".
    std::thread openThread_;
    std::atomic<bool> openPending_{false};
    std::atomic<int32_t> openOutcome_{0};
    EngineConfig pendingConfig_{};

    // Diagnóstico
    int32_t apiCode_ = -1;
    int32_t streamFormat_ = -1;
    int32_t streamRate_ = 0;
    int32_t streamChannels_ = 0;
    int32_t streamFrameBytes_ = 4;
    int32_t deviceId_ = 0;
    bool exclusive_ = false;
    bool mmap_ = false;
    mutable std::atomic<int32_t> errorCode_{0};
    std::atomic<int64_t> underruns_{0};
    std::atomic<int64_t> framesConsumed_{0};   // dominio stream, desde el último reset
    std::atomic<int64_t> framesQueued_{0};     // dominio stream, desde el último reset
    std::atomic<int64_t> framesDropped_{0};
    std::atomic<int64_t> lastLatencyFrames_{0};
    std::atomic<int64_t> lastPositionFrames_{0};
    std::atomic<int32_t> hardwareBufferFrames_{0};

    /**
     * Ancla del timestamp del HAL (idea tomada de Mornye, `native_audio.cpp`).
     *
     * AAudio publica el timestamp sólo cada varias ráfagas, así que entre refrescos la
     * posición se extrapola con el reloj; y si la posición del HAL salta hacia atrás (el
     * stream se reinició o el HAL cambió de ancla) se re-ancla en vez de devolver una
     * posición que retrocede. Sin esto el reloj del reproductor se mueve a saltos de
     * ~5 ms (se ve en la barra) y "vuelve atrás" al pausar.
     */
    std::atomic<int64_t> timestampAnchorNanos_{0};
    std::atomic<int64_t> timestampAnchorFrames_{0};

    // Formato de origen
    PcmFormat sourceFormat_ = PcmFormat::kI16;
    int32_t sourceRate_ = 0;
    int32_t sourceChannels_ = 2;

    bool passthrough_ = true;

    /**
     * Camino directo: con la señal intacta (mismo formato/rate/canales, volumen al
     * 100 %, sin fundidos pendientes) el bloque se copia **byte a byte** al anillo, sin
     * decodificar a float ni re-cuantizar. Es el criterio de bit-perfect de BitChord
     * (`BitPerfectPcm.convert` sólo llega al formato del DAC cuando hace falta): antes,
     * una pista de 24 bits siempre pasaba por float y volvía a enteros.
     */
    bool directPath_ = true;
    std::atomic<float> volume_{1.0f};
    std::atomic<int32_t> volumeCurve_{0};
    float volumeApplied_ = 1.0f;

    // Fundidos: lo escribe el hilo que pide (play/pause/flush), lo resuelve el callback.
    std::atomic<int32_t> fadeRequest_{kFadeNone};
    std::atomic<bool> holding_{false};
    std::atomic<bool> fades_{true};
    std::atomic<int32_t> fadeInFrames_{0};
    std::atomic<int32_t> fadeOutFrames_{0};
    std::atomic<int32_t> rearmFrames_{0};
    std::atomic<int32_t> prerollFrames_{0};
    std::atomic<bool> startPending_{false};
    std::atomic<bool> endOfStream_{false};

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
    PolyphaseResampler resampler_;
};

} // namespace audio

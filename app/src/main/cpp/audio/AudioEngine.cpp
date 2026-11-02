#include "audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <thread>

#include <android/log.h>

namespace audio {

namespace {

constexpr const char* kLogTag = "KukosinAudio";

void logI(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logI(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kLogTag, fmt, args);
    va_end(args);
}

void logW(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logW(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_WARN, kLogTag, fmt, args);
    va_end(args);
}

void logE(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logE(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, kLogTag, fmt, args);
    va_end(args);
}

/**
 * Tabla de fases del resampler.
 *
 * `kPhases`: cuántos valores de fase fraccionaria se tabulan. El error de fase residual
 * es ≤ 1/(2·kPhases) de muestra, así que 512 (lo que ya había) es holgado.
 *
 * `kBaseHalf` / `kKaiserBeta`: largo y ventana del filtro. Se subieron de 12 / β 8.0 a
 * 32 / β 10.06 siguiendo el criterio de BitChord (`DacFormatConverter`: ventana Kaiser
 * β ≈ 10 = ~100 dB de banda de rechazo). Medido en el arnés (`9a`/`9c`):
 *
 *   - 44.1 → 48 kHz: 20 kHz pasa de −0.94 dB a −0.08 dB (banda de paso plana).
 *   - 48 → 44.1 kHz: un tono de 23 kHz (que debe desaparecer) pasa de −11 dB
 *     —alias audible— a mejor que −40 dB.
 *
 * El costo es el doble de multiplicaciones por muestra, en el hilo productor y no en el
 * callback de audio: despreciable frente a decodificar el archivo.
 */
constexpr int32_t kPhases = 512;
constexpr double kKaiserBeta = 10.06;
constexpr int32_t kBaseHalf = 32;
constexpr int64_t kNanosPerSecond = 1000000000LL;

/**
 * Intentos de rate alternativo antes de rendirse (BitChord usa 3). Cada intento abre un
 * stream real: un DAC que no concede exclusivo los rechaza todos y encadenar más sólo
 * retrasa el arranque.
 */
constexpr int32_t MAX_RATE_ATTEMPTS = 3;

inline float clamp1(double x) {
    return static_cast<float>(x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x));
}

inline int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

oboe::AudioFormat toOboeFormat(PcmFormat format) {
    switch (format) {
        case PcmFormat::kI16: return oboe::AudioFormat::I16;
        case PcmFormat::kI24: return oboe::AudioFormat::I24;
        case PcmFormat::kI32: return oboe::AudioFormat::I32;
        case PcmFormat::kF32: return oboe::AudioFormat::Float;
    }
    return oboe::AudioFormat::I16;
}

PcmFormat fromOboeFormat(oboe::AudioFormat format) {
    switch (format) {
        case oboe::AudioFormat::I16: return PcmFormat::kI16;
        case oboe::AudioFormat::I24: return PcmFormat::kI24;
        case oboe::AudioFormat::I32: return PcmFormat::kI32;
        case oboe::AudioFormat::Float: return PcmFormat::kF32;
        default: return PcmFormat::kI16;
    }
}

// Decodifica PCM entrelazado a float normalizado (-1..1).
void decodeToFloat(const uint8_t* src, int32_t sampleCount, PcmFormat format, float* dst) {
    switch (format) {
        case PcmFormat::kI16: {
            const auto* s = reinterpret_cast<const int16_t*>(src);
            for (int32_t i = 0; i < sampleCount; ++i) dst[i] = s[i] / 32768.0f;
            break;
        }
        case PcmFormat::kI24: {
            for (int32_t i = 0; i < sampleCount; ++i) {
                const uint8_t* p = src + static_cast<size_t>(i) * 3;
                int32_t v = static_cast<int32_t>(p[0]) |
                            (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16);
                dst[i] = static_cast<float>(v / 8388608.0);
            }
            break;
        }
        case PcmFormat::kI32: {
            const auto* s = reinterpret_cast<const int32_t*>(src);
            for (int32_t i = 0; i < sampleCount; ++i) {
                dst[i] = static_cast<float>(static_cast<double>(s[i]) / 2147483648.0);
            }
            break;
        }
        case PcmFormat::kF32: {
            const auto* s = reinterpret_cast<const float*>(src);
            for (int32_t i = 0; i < sampleCount; ++i) dst[i] = s[i];
            break;
        }
    }
}

// Codifica float (-1..1) a PCM entrelazado, redondeando y saturando.
void encodeFromFloat(const float* src, int32_t sampleCount, PcmFormat format, uint8_t* dst) {
    switch (format) {
        case PcmFormat::kI16: {
            auto* d = reinterpret_cast<int16_t*>(dst);
            for (int32_t i = 0; i < sampleCount; ++i) {
                const double v = std::lrint(static_cast<double>(clamp1(src[i])) * 32768.0);
                d[i] = static_cast<int16_t>(v > 32767.0 ? 32767.0 : (v < -32768.0 ? -32768.0 : v));
            }
            break;
        }
        case PcmFormat::kI24: {
            for (int32_t i = 0; i < sampleCount; ++i) {
                double v = std::lrint(static_cast<double>(clamp1(src[i])) * 8388608.0);
                if (v > 8388607.0) v = 8388607.0;
                if (v < -8388608.0) v = -8388608.0;
                const int32_t iv = static_cast<int32_t>(v);
                uint8_t* p = dst + static_cast<size_t>(i) * 3;
                p[0] = static_cast<uint8_t>(iv & 0xFF);
                p[1] = static_cast<uint8_t>((iv >> 8) & 0xFF);
                p[2] = static_cast<uint8_t>((iv >> 16) & 0xFF);
            }
            break;
        }
        case PcmFormat::kI32: {
            auto* d = reinterpret_cast<int32_t*>(dst);
            for (int32_t i = 0; i < sampleCount; ++i) {
                double v = std::lrint(static_cast<double>(clamp1(src[i])) * 2147483648.0);
                if (v > 2147483647.0) v = 2147483647.0;
                if (v < -2147483648.0) v = -2147483648.0;
                d[i] = static_cast<int32_t>(v);
            }
            break;
        }
        case PcmFormat::kF32: {
            auto* d = reinterpret_cast<float*>(dst);
            for (int32_t i = 0; i < sampleCount; ++i) d[i] = clamp1(src[i]);
            break;
        }
    }
}

} // namespace

int32_t bytesPerSample(PcmFormat format) {
    switch (format) {
        case PcmFormat::kI16: return 2;
        case PcmFormat::kI24: return 3;
        case PcmFormat::kI32: return 4;
        case PcmFormat::kF32: return 4;
    }
    return 4;
}

const char* formatName(PcmFormat format) {
    switch (format) {
        case PcmFormat::kI16: return "16-bit";
        case PcmFormat::kI24: return "24-bit";
        case PcmFormat::kI32: return "32-bit";
        case PcmFormat::kF32: return "float32";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// ByteRingBuffer
// ---------------------------------------------------------------------------

ByteRingBuffer::ByteRingBuffer() { resize(1u << 18); }

void ByteRingBuffer::resize(size_t capacity) {
    // Potencia de dos para usar máscara en vez de módulo.
    size_t cap = 1;
    while (cap < capacity) cap <<= 1;
    if (cap == capacity_) return;
    clear();
    buffer_.assign(cap, 0);
    capacity_ = cap;
    mask_ = cap - 1;
    readIndex_.store(0, std::memory_order_relaxed);
    writeIndex_.store(0, std::memory_order_relaxed);
}

size_t ByteRingBuffer::write(const uint8_t* data, size_t count) {
    if (count == 0 || capacity_ == 0) return 0;
    const size_t readIndex = readIndex_.load(std::memory_order_acquire);
    const size_t writeIndex = writeIndex_.load(std::memory_order_relaxed);
    const size_t space = capacity_ - (writeIndex - readIndex);
    const size_t n = std::min(count, space);
    if (n == 0) return 0;
    const size_t w = writeIndex & mask_;
    const size_t first = std::min(n, capacity_ - w);
    std::memcpy(buffer_.data() + w, data, first);
    if (n > first) {
        std::memcpy(buffer_.data(), data + first, n - first);
    }
    writeIndex_.store(writeIndex + n, std::memory_order_release);
    return n;
}

size_t ByteRingBuffer::read(uint8_t* out, size_t count) {
    if (count == 0 || capacity_ == 0) return 0;
    const size_t readIndex = readIndex_.load(std::memory_order_relaxed);
    const size_t writeIndex = writeIndex_.load(std::memory_order_acquire);
    const size_t available = writeIndex - readIndex;
    const size_t n = std::min(count, available);
    if (n == 0) return 0;
    const size_t r = readIndex & mask_;
    const size_t first = std::min(n, capacity_ - r);
    std::memcpy(out, buffer_.data() + r, first);
    if (n > first) {
        std::memcpy(out + first, buffer_.data(), n - first);
    }
    readIndex_.store(readIndex + n, std::memory_order_release);
    return n;
}

void ByteRingBuffer::clear() {
    readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
}

size_t ByteRingBuffer::availableToRead() const {
    const size_t w = writeIndex_.load(std::memory_order_acquire);
    const size_t r = readIndex_.load(std::memory_order_acquire);
    return w - r;
}

size_t ByteRingBuffer::freeSpace() const {
    return capacity_ - availableToRead();
}

// ---------------------------------------------------------------------------
// PolyphaseResampler
// ---------------------------------------------------------------------------

double PolyphaseResampler::besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double y = x * x / 4.0;
    for (int32_t i = 1; i < 60; ++i) {
        term *= y / (static_cast<double>(i) * static_cast<double>(i));
        sum += term;
        if (term < sum * 1e-14) break;
    }
    return sum;
}

void PolyphaseResampler::configure(int32_t inRate, int32_t outRate, int32_t channels) {
    channels_ = channels > 0 ? channels : 2;
    step_ = (inRate > 0 && outRate > 0) ? static_cast<double>(inRate) / static_cast<double>(outRate)
                                        : 1.0;
    half_ = static_cast<int32_t>(std::ceil(kBaseHalf * std::max(1.0, step_)));
    taps_ = half_ * 2;
    rebuildKernel();
    reset();
}

void PolyphaseResampler::rebuildKernel() {
    kernel_.assign(static_cast<size_t>(kPhases) * static_cast<size_t>(taps_), 0.0f);
    // El corte depende de la dirección, y equivocarlo es audible:
    //  - Bajando de tasa (step > 1) hay que cortar por debajo del Nyquist de SALIDA y
    //    dejar banda de transición (0.96), o los tonos agudos por encima de ese Nyquist
    //    se pliegan dentro del audio audible como alias.
    //  - Subiendo de tasa (step < 1) el Nyquist que manda es el de ENTRADA: el filtro
    //    debe ser plano hasta él. Aplicar el 0.96 también al subir atenuaba los agudos
    //    (medido: −0.9 dB a 20 kHz, y peor con ventanas más largas).
    const double fc = step_ > 1.0 ? 0.5 * 0.96 / step_ : 0.5;
    const double norm = besselI0(kKaiserBeta);
    for (int32_t ph = 0; ph < kPhases; ++ph) {
        const double frac = static_cast<double>(ph) / static_cast<double>(kPhases);
        double sum = 0.0;
        for (int32_t t = 0; t < taps_; ++t) {
            const double x = static_cast<double>(t - half_ + 1) - frac;
            const double arg = 2.0 * fc * x;
            const double sinc = std::fabs(arg) < 1e-9 ? 1.0 : std::sin(M_PI * arg) / (M_PI * arg);
            const double r = x / static_cast<double>(half_);
            const double window = std::fabs(r) >= 1.0
                    ? 0.0
                    : besselI0(kKaiserBeta * std::sqrt(1.0 - r * r)) / norm;
            const double v = 2.0 * fc * sinc * window;
            kernel_[static_cast<size_t>(ph) * taps_ + t] = static_cast<float>(v);
            sum += v;
        }
        if (std::fabs(sum) > 1e-12) {
            for (int32_t t = 0; t < taps_; ++t) {
                const size_t idx = static_cast<size_t>(ph) * taps_ + t;
                kernel_[idx] = static_cast<float>(kernel_[idx] / sum);
            }
        }
    }
}

void PolyphaseResampler::reset() {
    historyFrames_ = 0;
    pos_ = std::max(0, half_ - 1);
    work_.assign(static_cast<size_t>(std::max(taps_ * 2, 64)) * channels_, 0.0f);
}

int32_t PolyphaseResampler::process(const float* in, int32_t inFrames, float* out,
                                    int32_t maxOutFrames) {
    if (inFrames <= 0 || maxOutFrames <= 0) return 0;

    if (bypass()) {
        const int32_t n = std::min(inFrames, maxOutFrames);
        std::memcpy(out, in, sizeof(float) * static_cast<size_t>(n) * channels_);
        pos_ = 1.0;
        historyFrames_ = 0;
        return n;
    }

    const int32_t histSamples = historyFrames_ * channels_;
    const int32_t inSamples = inFrames * channels_;
    const size_t workSamples = static_cast<size_t>(histSamples + inSamples);
    if (work_.size() < workSamples) work_.resize(workSamples, 0.0f);
    std::memcpy(work_.data() + histSamples, in, sizeof(float) * static_cast<size_t>(inSamples));

    const int32_t workFrames = historyFrames_ + inFrames;
    int32_t outFrames = 0;

    while (outFrames < maxOutFrames) {
        const int32_t base = static_cast<int32_t>(std::floor(pos_));
        if (base + half_ >= workFrames) break;
        const double frac = pos_ - static_cast<double>(base);
        int32_t phase = static_cast<int32_t>(frac * kPhases);
        if (phase < 0) phase = 0;
        if (phase >= kPhases) phase = kPhases - 1;
        const float* k = kernel_.data() + static_cast<size_t>(phase) * taps_;
        const float* srcFrame = work_.data() + static_cast<size_t>(base - half_ + 1) * channels_;
        for (int32_t c = 0; c < channels_; ++c) {
            float acc = 0.0f;
            const float* s = srcFrame + c;
            for (int32_t t = 0; t < taps_; ++t) {
                acc += s[static_cast<size_t>(t) * channels_] * k[t];
            }
            out[static_cast<size_t>(outFrames) * channels_ + c] = acc;
        }
        ++outFrames;
        pos_ += step_;
    }

    // Compactar conservando la ventana que necesita la próxima llamada. Se conserva
    // siempre `half_ - 1` frames de historia para que el próximo bloque pueda
    // interpolar desde su primer frame (antes podía quedarse corto y perder muestras).
    const int32_t processedUpTo = static_cast<int32_t>(std::floor(pos_));
    const int32_t keepFrom = std::max(0, processedUpTo - half_ + 1);
    if (keepFrom > 0 && keepFrom <= workFrames) {
        const int32_t keepFrames = workFrames - keepFrom;
        std::memmove(work_.data(),
                     work_.data() + static_cast<size_t>(keepFrom) * channels_,
                     sizeof(float) * static_cast<size_t>(keepFrames) * channels_);
        historyFrames_ = keepFrames;
        pos_ -= keepFrom;
    } else {
        historyFrames_ = workFrames;
    }
    return outFrames;
}

// ---------------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { close(); }

oboe::Result AudioEngine::openStream(PcmFormat format,
                                     int32_t sampleRate,
                                     int32_t channels,
                                     oboe::SharingMode sharing,
                                     oboe::PerformanceMode performance) {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    // Unspecified: Oboe elige AAudio en API 26+ y OpenSL ES en equipos antiguos.
    builder.setAudioApi(oboe::AudioApi::Unspecified);
    builder.setFormat(toOboeFormat(format));
    builder.setSampleRate(sampleRate > 0 ? sampleRate : 48000);
    builder.setChannelCount(channels > 0 ? channels : 2);
    builder.setSharingMode(sharing);
    builder.setPerformanceMode(performance);
    builder.setUsage(oboe::Usage::Media);
    builder.setContentType(oboe::ContentType::Music);
    builder.setSpatializationBehavior(oboe::SpatializationBehavior::Never);
    builder.setIsContentSpatialized(false);
    // Para bit-perfect: prohibido que Oboe convierta por su cuenta. Si el formato
    // no es nativo, preferimos fallar y probar otro candidato nosotros.
    builder.setFormatConversionAllowed(false);
    builder.setChannelConversionAllowed(false);
    builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
    builder.setDataCallback(this);
    builder.setErrorCallback(this);
    if (deviceId_ > 0) {
        builder.setDeviceId(deviceId_);
    }
    std::shared_ptr<oboe::AudioStream> stream;
    const oboe::Result result = builder.openStream(stream);
    if (result != oboe::Result::OK) {
        return result;
    }

    // `Exclusive` y el formato se **piden**, no se garantizan: Android puede devolver un
    // stream compartido o con otro formato sin avisar, y la UI terminaría mostrando
    // "exclusivo" cuando no lo es (Mornye hace esta misma comprobación y descarta el
    // stream en vez de mentir). Si el modo exige exclusivo y no lo consiguió, se cierra
    // y se sigue con el próximo candidato.
    if (sharing == oboe::SharingMode::Exclusive &&
        stream->getSharingMode() != oboe::SharingMode::Exclusive) {
        logW("el HAL no concedió exclusivo (devolvió %d, donde 0=shared y 1=exclusive): "
             "se descarta este stream",
             static_cast<int>(stream->getSharingMode()));
        stream->close();
        return oboe::Result::ErrorUnavailable;
    }
    if (stream->getFormat() != toOboeFormat(format) ||
        stream->getSampleRate() != (sampleRate > 0 ? sampleRate : 48000) ||
        stream->getChannelCount() != (channels > 0 ? channels : 2)) {
        logW("el HAL cambió el formato pedido (%s %d Hz %d ch -> %s %d Hz %d ch)",
             formatName(format), sampleRate, channels,
             formatName(fromOboeFormat(stream->getFormat())), stream->getSampleRate(),
             stream->getChannelCount());
    }
    stream_ = stream;
    return oboe::Result::OK;
}

oboe::Result AudioEngine::tryOpenCandidates(const EngineConfig& config) {
    const oboe::SharingMode strictSharing = oboe::SharingMode::Exclusive;
    const oboe::SharingMode looseSharing = config.requireExclusive ? oboe::SharingMode::Exclusive
                                                                 : oboe::SharingMode::Shared;
    oboe::Result last = oboe::Result::ErrorUnavailable;

    // 1) Exacto + exclusivo + low latency (camino MMAP si el equipo lo permite).
    last = openStream(sourceFormat_, sourceRate_, sourceChannels_,
                      strictSharing, oboe::PerformanceMode::LowLatency);
    if (last == oboe::Result::OK) return last;
    logW("exclusivo %s @ %d falló: %s", formatName(sourceFormat_), sourceRate_,
         oboe::convertToText(last));

    // 2) Exacto, pero con la política de sharing que permite el modo.
    last = openStream(sourceFormat_, sourceRate_, sourceChannels_,
                      looseSharing, oboe::PerformanceMode::LowLatency);
    if (last == oboe::Result::OK) return last;
    logW("compartido %s @ %d falló: %s", formatName(sourceFormat_), sourceRate_,
         oboe::convertToText(last));

    // 3) Exacto sin exigir low latency (algunos HAL antiguos rechazan MMAP).
    last = openStream(sourceFormat_, sourceRate_, sourceChannels_,
                      looseSharing, oboe::PerformanceMode::None);
    if (last == oboe::Result::OK) return last;
    logW("sin low-latency %s @ %d falló: %s", formatName(sourceFormat_), sourceRate_,
         oboe::convertToText(last));

    // 4) Rate nativo del equipo con el formato de la pista: aquí todavía no hay
    //    conversión de muestras, sólo de rate en el HAL (se informa como !passthrough
    //    porque nosotros resampleamos). Sólo si el modo no es estricto. Se prueban
    //    **pocos** candidatos (idea de BitChord, `MAX_RATE_ATTEMPTS = 3`): cada intento
    //    abre un stream real en el hilo de apertura y un DAC que niega exclusivo los
    //    niega todos, así que encadenar seis sólo demora el arranque.
    if (!config.strictBitPerfect) {
        const int32_t rates[] = {48000, 44100, 96000, 192000};
        int attempts = 0;
        for (int32_t rate : rates) {
            if (rate == sourceRate_) continue;
            if (attempts >= MAX_RATE_ATTEMPTS) break;
            ++attempts;
            last = openStream(sourceFormat_, rate, sourceChannels_,
                              looseSharing, oboe::PerformanceMode::LowLatency);
            if (last == oboe::Result::OK) {
                logI("rate de fallback aceptado: %s @ %d (fuente %d Hz)",
                     formatName(sourceFormat_), rate, sourceRate_);
                return last;
            }
        }

        // 5) Formato nativo del equipo. Se codifica desde el float interno, así que
        //    el resultado sigue siendo de alta calidad (redondeo, sin doble conversión).
        const PcmFormat formats[] = {PcmFormat::kI32, PcmFormat::kI24, PcmFormat::kI16,
                                     PcmFormat::kF32};
        for (PcmFormat format : formats) {
            if (format == sourceFormat_) continue;
            last = openStream(format, sourceRate_, sourceChannels_,
                              looseSharing, oboe::PerformanceMode::LowLatency);
            if (last == oboe::Result::OK) {
                logI("formato de fallback aceptado: %s @ %d (fuente %s)",
                     formatName(format), sourceRate_, formatName(sourceFormat_));
                return last;
            }
        }
    }
    return last;
}

oboe::Result AudioEngine::open(const EngineConfig& config) {
    // El hilo anterior debe haber terminado ANTES de tomar el mutex: si se uniera con
    // el mutex en la mano, y ese hilo estuviera esperándolo para publicar su resultado,
    // ambos se bloquearían (deadlock).
    joinOpenThread();

    {
        std::lock_guard<std::mutex> guard(controlMutex_);
        closeLocked();   // sin join: ya se hizo arriba

        sourceFormat_ = config.sourceFormat;
        sourceChannels_ = config.sourceChannels > 0 ? config.sourceChannels : 2;
        sourceRate_ = config.sourceSampleRate > 0 ? config.sourceSampleRate : 48000;
        deviceId_ = config.deviceId;
        errorCode_.store(0, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
        resetStreamAccounting();
        pendingConfig_ = config;
        openOutcome_.store(0, std::memory_order_relaxed);
        openPending_.store(true, std::memory_order_release);
    }

    logI("open (async): source=%s %d Hz %d ch strict=%d exclusiveOnly=%d requireDevice=%d "
         "buffer=%dms device=%d",
         formatName(config.sourceFormat),
         config.sourceSampleRate > 0 ? config.sourceSampleRate : 48000,
         config.sourceChannels > 0 ? config.sourceChannels : 2,
         config.strictBitPerfect ? 1 : 0, config.requireExclusive ? 1 : 0,
         config.requireDevice ? 1 : 0, config.bufferMs, config.deviceId);

    openThread_ = std::thread([this] { openThreadMain(); });

    // Se devuelve OK en el sentido de "la configuración quedó aceptada": el resultado
    // real de la apertura se consulta con awaitOpen() (o en el primer write/start, que
    // es donde Media3 puede reaccionar con WriteException).
    return oboe::Result::OK;
}

/**
 * Cuerpo de la apertura, en su propio hilo. Nunca toca `stream_` sin el mutex de
 * control, y publica el resultado en `openOutcome_` para que el productor lo vea.
 */
void AudioEngine::openThreadMain() {
    oboe::Result result = oboe::Result::ErrorNull;

    const EngineConfig config = pendingConfig_;
    std::shared_ptr<oboe::AudioStream> stream;
    {
        std::lock_guard<std::mutex> guard(controlMutex_);
        if (!openPending_.load(std::memory_order_acquire)) {
            openOutcome_.store(static_cast<int32_t>(oboe::Result::ErrorNull),
                               std::memory_order_release);
            return;
        }
        // Reservar el estado de diagnóstico antes de abrir.
        deviceId_ = config.deviceId;
        result = tryOpenCandidates(config);
        if (result != oboe::Result::OK && deviceId_ > 0 && !config.requireDevice) {
            logW("el dispositivo %d no se pudo abrir; se reintenta con el predeterminado",
                 deviceId_);
            deviceId_ = 0;
            result = tryOpenCandidates(config);
        }
        if (result == oboe::Result::OK) {
            stream = stream_;
            finalizeOpen(config);
        } else {
            logE("no se pudo abrir ningún stream: %s", oboe::convertToText(result));
            errorCode_.store(static_cast<int32_t>(result), std::memory_order_relaxed);
        }
    }
    openOutcome_.store(static_cast<int32_t>(result), std::memory_order_release);
    openPending_.store(false, std::memory_order_release);
}

void AudioEngine::joinOpenThread() {
    if (openThread_.joinable()) {
        openThread_.join();
    }
}

bool AudioEngine::awaitOpen(int32_t timeoutMs) {
    const int64_t deadline = nowNanos() + static_cast<int64_t>(timeoutMs) * 1000000LL;
    while (openPending_.load(std::memory_order_acquire)) {
        if (nowNanos() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const int32_t outcome = openOutcome_.load(std::memory_order_acquire);
    if (outcome != 0) {
        errorCode_.store(outcome, std::memory_order_relaxed);
        return false;
    }
    return stream_ != nullptr;
}

void AudioEngine::finalizeOpen(const EngineConfig& config) {
    streamFormat_ = static_cast<int32_t>(fromOboeFormat(stream_->getFormat()));
    streamRate_ = stream_->getSampleRate();
    streamChannels_ = stream_->getChannelCount();
    streamFrameBytes_ = streamChannels_ * bytesPerSample(static_cast<PcmFormat>(streamFormat_));
    if (streamFrameBytes_ <= 0) streamFrameBytes_ = 4;
    apiCode_ = stream_->getAudioApi() == oboe::AudioApi::AAudio ? 0 : 1;
    exclusive_ = stream_->getSharingMode() == oboe::SharingMode::Exclusive;
    mmap_ = false;
#ifdef AUDIOENGINE_HAVE_OBOE_EXTENSIONS
    mmap_ = oboe::OboeExtensions::isMMapUsed(stream_.get());
#endif
    deviceId_ = stream_->getDeviceId() > 0 ? stream_->getDeviceId() : deviceId_;

    passthrough_ = static_cast<PcmFormat>(streamFormat_) == sourceFormat_ &&
                   streamRate_ == sourceRate_ &&
                   streamChannels_ == sourceChannels_;

    resampler_.configure(sourceRate_, streamRate_, streamChannels_);
    volumeApplied_ = volume_.load(std::memory_order_relaxed);

    // El camino directo sólo es válido si no hay nada que transformar. Con
    // `fades` activo el callback escala la señal (fundido de entrada/salida), así que
    // el bit-perfect real exige fundidos apagados — igual que el volumen al 100 %.
    directPath_ = passthrough_ && !config.fades;

    // Fundidos: 25 ms de entrada y 60 ms de salida. Con fades apagados la señal sale
    // intacta desde el primer frame (modo purista).
    fades_.store(config.fades, std::memory_order_relaxed);
    const bool fades = config.fades;
    fadeInFrames_.store(fades ? std::max(24, streamRate_ * 25 / 1000) : 0,
                        std::memory_order_relaxed);
    fadeOutFrames_.store(fades ? std::max(48, streamRate_ * 60 / 1000) : 0,
                         std::memory_order_relaxed);
    rearmFrames_.store(fades ? std::max(16, streamRate_ * 12 / 1000) : 0,
                       std::memory_order_relaxed);
    gain_ = fades ? 0.0f : 1.0f;
    gainStep_ = 0.0f;
    stopAfterFade_ = false;
    fadeRequest_.store(kFadeNone, std::memory_order_relaxed);
    holding_.store(false, std::memory_order_relaxed);
    endOfStream_.store(false, std::memory_order_relaxed);

    // Reparto de buffers, que es lo que define la velocidad percibida:
    //  - ABAJO (AAudio) el buffer más chico posible: 1-2 ráfagas. Es lo que se tira
    //    cuando hay un salto y lo que hay que rellenar después.
    //  - ARRIBA (nuestro anillo) el buffer grande del ajuste "Buffer de salida": ahí
    //    vive la estabilidad, y limpiarlo es instantáneo porque es memoria nuestra.
    const int32_t burst = stream_->getFramesPerBurst() > 0 ? stream_->getFramesPerBurst() : 192;
    const int32_t capacity = stream_->getBufferCapacityInFrames();
    int32_t requestedFrames = exclusive_ ? burst * 2 : burst * 3;
    if (mmap_) requestedFrames = burst;      // MMAP: una ráfaga ya es suficiente
    requestedFrames = std::max(requestedFrames, burst);
    if (capacity > 0) requestedFrames = std::min(capacity, requestedFrames);
    int32_t actualFrames = requestedFrames;
    if (requestedFrames > 0) {
        stream_->setBufferSizeInFrames(requestedFrames);
        const int32_t actual = stream_->getBufferSizeInFrames();
        if (actual > 0) actualFrames = actual;
    }
    hardwareBufferFrames_.store(actualFrames, std::memory_order_relaxed);

    // Cuánto audio hace falta para arrancar el stream: dos ráfagas o 20 ms, lo que
    // sea mayor. Arrancar con el anillo vacío sólo produce silencio y xruns.
    // Preroll: con `prerollMs` configurado se respeta (los modos "sin cortes" bajan a una
    // ráfaga para arrancar de inmediato); si no, el de siempre (2 ráfagas o 20 ms).
    const int32_t configuredPrerollMs = config.prerollMs;
    const int32_t prerollFromMs = configuredPrerollMs >= 0
            ? std::max(burst, streamRate_ * configuredPrerollMs / 1000)
            : std::max(burst * 2, streamRate_ * 20 / 1000);
    prerollFrames_.store(prerollFromMs, std::memory_order_relaxed);
    startPending_.store(false, std::memory_order_relaxed);

    // Ring: 4 x buffer, mínimo 256 KB, máximo 4 MB.
    const size_t bytesPerSecond = static_cast<size_t>(streamRate_) *
                                  static_cast<size_t>(streamFrameBytes_);
    const int32_t bufferMs = std::max(config.bufferMs, 200);
    size_t ringBytes = bytesPerSecond * static_cast<size_t>(bufferMs) / 1000 * 4;
    ringBytes = std::max<size_t>(ringBytes, 1u << 18);
    ringBytes = std::min<size_t>(ringBytes, 1u << 22);
    ring_.resize(ringBytes);

    logI("opened: api=%d format=%s rate=%d ch=%d burst=%d hwBuffer=%d ring=%zuKB preroll=%d "
         "exclusive=%d mmap=%d passthrough=%d",
         apiCode_, formatName(static_cast<PcmFormat>(streamFormat_)), streamRate_,
         streamChannels_, burst, actualFrames, ringBytes / 1024,
         prerollFrames_.load(std::memory_order_relaxed), exclusive_ ? 1 : 0, mmap_ ? 1 : 0,
         passthrough_ ? 1 : 0);
}

oboe::Result AudioEngine::close() {
    // Fuera del mutex: el hilo de apertura puede estar esperándolo.
    joinOpenThread();
    std::lock_guard<std::mutex> guard(controlMutex_);
    closeLocked();
    return oboe::Result::OK;
}

void AudioEngine::closeLocked() {
    openPending_.store(false, std::memory_order_release);
    {
        std::shared_ptr<oboe::AudioStream> stream = stream_;
        stream_.reset();
        if (stream) {
            stream->requestStop();
            stream->close();
        }
    }
    ring_.clear();
    srcFloats_.clear();
    resampled_.clear();
    mapped_.clear();
    outBytes_.clear();
    apiCode_ = -1;
    streamFormat_ = -1;
    streamRate_ = 0;
    streamChannels_ = 0;
    passthrough_ = true;
    mmap_ = false;
    exclusive_ = false;
    fadeRequest_.store(kFadeNone, std::memory_order_relaxed);
    holding_.store(true, std::memory_order_relaxed);
    startPending_.store(false, std::memory_order_relaxed);
    endOfStream_.store(false, std::memory_order_relaxed);
    gain_ = 1.0f;
    gainStep_ = 0.0f;
    stopAfterFade_ = false;
    resetStreamAccounting();
}

void AudioEngine::resetStreamAccounting() {
    timestampAnchorNanos_.store(0, std::memory_order_relaxed);
    timestampAnchorFrames_.store(0, std::memory_order_relaxed);
    framesConsumed_.store(0, std::memory_order_relaxed);
    framesQueued_.store(0, std::memory_order_relaxed);
    framesDropped_.store(0, std::memory_order_relaxed);
    lastLatencyFrames_.store(0, std::memory_order_relaxed);
    lastPositionFrames_.store(0, std::memory_order_relaxed);
}

oboe::Result AudioEngine::setStreamState(int32_t target) {
    if (!stream_) return oboe::Result::ErrorInvalidState;
    oboe::Result result;
    switch (target) {
        case 0: result = stream_->requestStart(); break;
        case 1: result = stream_->requestPause(); break;
        case 2: result = stream_->requestStop(); break;
        case 3: result = stream_->requestFlush(); break;
        default: return oboe::Result::ErrorInvalidState;
    }
    if (result != oboe::Result::OK) {
        logW("transición %d falló: %s", target, oboe::convertToText(result));
    }
    return result;
}

bool AudioEngine::waitForState(std::initializer_list<oboe::StreamState> states,
                               int32_t timeoutMs) {
    if (!stream_) return false;
    const int64_t deadline = nowNanos() + static_cast<int64_t>(timeoutMs) * 1000000LL;
    oboe::StreamState current = stream_->getState();
    for (;;) {
        for (oboe::StreamState wanted : states) {
            if (current == wanted) return true;
        }
        if (current == oboe::StreamState::Closed || current == oboe::StreamState::Disconnected) {
            return false;
        }
        const int64_t remainingNs = deadline - nowNanos();
        if (remainingNs <= 0) return false;
        const int64_t sliceNs = std::min<int64_t>(remainingNs, 5 * 1000000LL);
        oboe::StreamState nextState = current;
        if (stream_->waitForStateChange(current, &nextState, sliceNs) != oboe::Result::OK) {
            // Timeout del slice o stream cerrado: se reevalúa el estado real.
            if (stream_->getState() == current && deadline - nowNanos() <= 0) return false;
        }
        current = nextState;
    }
}

oboe::Result AudioEngine::start() {
    if (openPending_.load(std::memory_order_acquire) && !awaitOpen()) {
        return static_cast<oboe::Result>(errorCode_.load(std::memory_order_relaxed));
    }
    std::lock_guard<std::mutex> guard(controlMutex_);
    // El fundido de entrada lo aplica el callback: acá sólo se pide.
    fadeRequest_.store(fades_.load(std::memory_order_relaxed) ? kFadeIn : kFadeNone,
                       std::memory_order_release);
    holding_.store(false, std::memory_order_release);
    endOfStream_.store(false, std::memory_order_release);
    stopAfterFade_ = false;
    if (!stream_) return oboe::Result::ErrorInvalidState;
    // No se arranca "en seco": se arranca cuando el anillo ya tiene con qué sonar.
    startPending_.store(true, std::memory_order_release);
    return maybeStart();
}

oboe::Result AudioEngine::maybeStart() {
    if (!stream_ || !startPending_.load(std::memory_order_acquire)) return oboe::Result::OK;
    if (stream_->getState() == oboe::StreamState::Started) {
        startPending_.store(false, std::memory_order_release);
        return oboe::Result::OK;
    }
    const size_t needBytes =
            static_cast<size_t>(prerollFrames_.load(std::memory_order_relaxed)) *
            static_cast<size_t>(streamFrameBytes_);
    if (needBytes > 0 && ring_.availableToRead() < needBytes) {
        return oboe::Result::OK;   // todavía no hay datos: se reintenta desde writePacked
    }
    startPending_.store(false, std::memory_order_release);
    return setStreamState(0);
}

oboe::Result AudioEngine::pause() {
    std::lock_guard<std::mutex> guard(controlMutex_);
    startPending_.store(false, std::memory_order_release);
    if (!stream_) return oboe::Result::ErrorInvalidState;
    if (stream_->getState() != oboe::StreamState::Started) {
        // Ya está parado: no hay nada que fundir.
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return oboe::Result::OK;
    }
    if (!fades_.load(std::memory_order_relaxed)) {
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return setStreamState(1);
    }
    // 60 ms de rampa y el callback se detiene solo (DataCallbackResult::Stop).
    fadeRequest_.store(kFadeOutAndHold, std::memory_order_release);
    return oboe::Result::OK;
}

oboe::Result AudioEngine::stop() {
    std::lock_guard<std::mutex> guard(controlMutex_);
    startPending_.store(false, std::memory_order_release);
    if (!stream_) return oboe::Result::ErrorInvalidState;

    // `AudioOutput.stop()` de Media3 significa "deja sonar lo que queda y para"
    // (`DefaultAudioSink.playToEndOfStream()` llama a `stop()`). Por eso NO se vacía
    // el anillo ni se pide requestStop(): el callback sigue vaciando el ring y se
    // detiene él mismo (DataCallbackResult::Stop) cuando el DAC ya lo consumió.
    // Si acá se llamara requestStop(), el HAL descartaría el buffer que todavía no
    // sonó y se cortarían muestras al final de cada pista.
    endOfStream_.store(true, std::memory_order_release);
    if (stream_->getState() != oboe::StreamState::Started) {
        // Ya está detenido: nada que drenar.
        holding_.store(true, std::memory_order_relaxed);
        return oboe::Result::OK;
    }
    if (ring_.availableToRead() == 0) {
        holding_.store(true, std::memory_order_relaxed);
        return oboe::Result::OK;   // el callback parará en el próximo bloque
    }
    // Fade de salida corto SOLO al final del stream (60 ms). Con fades apagados no se
    // toca la señal: se deja que el callback agote el anillo tal cual.
    if (fades_.load(std::memory_order_relaxed)) {
        fadeRequest_.store(kFadeOutAndHold, std::memory_order_release);
    }
    return oboe::Result::OK;
}

oboe::Result AudioEngine::flush() {
    return flushInternal(/*waitForHardware=*/true);
}

oboe::Result AudioEngine::flushForTransition() {
    return flushInternal(/*waitForHardware=*/false);
}

oboe::Result AudioEngine::flushInternal(bool waitForHardware) {
    std::lock_guard<std::mutex> guard(controlMutex_);
    startPending_.store(false, std::memory_order_release);
    endOfStream_.store(false, std::memory_order_release);
    holding_.store(true, std::memory_order_relaxed);
    stopAfterFade_ = false;

    // 1) Los datos propios se descartan siempre: es memoria nuestra y es instantáneo.
    const size_t dropped = ring_.availableToRead();
    ring_.clear();
    resampler_.reset();
    if (streamFrameBytes_ > 0) {
        framesDropped_.fetch_add(static_cast<int64_t>(dropped / streamFrameBytes_),
                                 std::memory_order_relaxed);
    }
    resetStreamAccounting();

    if (!stream_) return oboe::Result::OK;

    const oboe::StreamState current = stream_->getState();
    if (current == oboe::StreamState::Closed || current == oboe::StreamState::Disconnected) {
        return oboe::Result::OK;
    }

    // 2) AAudio sólo acepta requestFlush() en OPEN/PAUSED/STOPPED/FLUSHED, así que se
    //    pausa primero y se espera PAUSED (antes se llamaba desde STARTED, donde la
    //    petición no tiene efecto y el HAL podía seguir sonando lo viejo tras un seek).
    if (current == oboe::StreamState::Started ||
        current == oboe::StreamState::Starting ||
        current == oboe::StreamState::Pausing ||
        current == oboe::StreamState::Flushing) {
        if (setStreamState(1) != oboe::Result::OK) {
            setStreamState(2);
        }
        if (waitForHardware) {
            if (!waitForState({oboe::StreamState::Paused, oboe::StreamState::Stopped}, 120)) {
                logW("flush: el stream no llegó a PAUSED; se descarta la operación");
            }
        }
    }

    const oboe::StreamState ready = stream_->getState();
    oboe::Result result = oboe::Result::OK;
    if (ready == oboe::StreamState::Paused || ready == oboe::StreamState::Stopped ||
        ready == oboe::StreamState::Open) {
        result = setStreamState(3);
        if (result == oboe::Result::OK && waitForHardware) {
            waitForState({oboe::StreamState::Flushed, oboe::StreamState::Paused}, 120);
        }
    } else if (ready != oboe::StreamState::Flushed && waitForHardware) {
        result = oboe::Result::ErrorInvalidState;
        logW("flush: estado inesperado %d", static_cast<int32_t>(ready));
    }

    // 3) El stream queda en PAUSED (no FLUSHED) porque es el estado desde el que
    //    `play()` puede reanudar sin reabrir: Media3 reutiliza el mismo AudioOutput
    //    después de un seek, y un stream FLUSHED no arranca con requestStart().
    const oboe::StreamState afterFlush = stream_->getState();
    if (afterFlush != oboe::StreamState::Paused) {
        if (afterFlush == oboe::StreamState::Flushed) {
            setStreamState(1);   // Flushed -> Paused (válido y poco costoso)
            if (waitForHardware) waitForState({oboe::StreamState::Paused}, 60);
        } else if (afterFlush == oboe::StreamState::Started) {
            setStreamState(1);
            waitForState({oboe::StreamState::Paused}, 60);
        }
    }

    // 4) Estado limpio de la señal: el próximo arranque entra con fundido corto.
    gain_ = fades_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    gainStep_ = 0.0f;
    fadeRequest_.store(fades_.load(std::memory_order_relaxed) ? kFadeRearm : kFadeNone,
                       std::memory_order_release);
    return result;
}

bool AudioEngine::drain(int32_t timeoutMs) {
    const int64_t deadline = nowNanos() + static_cast<int64_t>(timeoutMs) * 1000000LL;
    for (;;) {
        if (stream_ == nullptr) return true;
        const int64_t queued = framesQueued_.load(std::memory_order_relaxed);
        if (queued <= 0) return true;
        if (positionFrames() >= queued) return true;
        if (nowNanos() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void AudioEngine::setVolume(float volume) {
    volume_.store(std::min(std::max(volume, 0.0f), 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setVolumeCurve(int32_t curve) {
    volumeCurve_.store(curve == 1 ? 1 : 0, std::memory_order_relaxed);
}

float AudioEngine::targetGain() const {
    const float volume = volume_.load(std::memory_order_relaxed);
    if (volumeCurve_.load(std::memory_order_relaxed) == 0) return volume;
    // Curva perceptual: la sensación de volumen del oído es aproximadamente logarítmica,
    // así que una recta en el deslizador suena como "nada, nada, todo". Con v^2,5 el
    // recorrido se reparte parejo. Al 100 % el resultado es exactamente 1.0, así que el
    // camino bit-perfect no se toca.
    return std::pow(volume, 2.5f);
}

void AudioEngine::setFadesEnabled(bool enabled) {
    if (!enabled) {
        fades_.store(false, std::memory_order_relaxed);
        gain_ = 1.0f;
        gainStep_ = 0.0f;
        stopAfterFade_ = false;
    }
}

void AudioEngine::updatePlayoutPosition(bool forceZeroLatency) {
    if (!stream_) {
        lastPositionFrames_.store(framesConsumed_.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        return;
    }
    const int64_t consumed = framesConsumed_.load(std::memory_order_relaxed);
    int64_t latencyFrames = lastLatencyFrames_.load(std::memory_order_relaxed);

    if (!forceZeroLatency && stream_->getState() == oboe::StreamState::Started) {
        int64_t hardwarePosition = 0;
        int64_t hardwareTime = 0;
        if (stream_->getTimestamp(CLOCK_MONOTONIC, &hardwarePosition, &hardwareTime) ==
            oboe::Result::OK) {
            const int64_t measured = consumed - hardwarePosition;
            const int64_t upperBound =
                    static_cast<int64_t>(hardwareBufferFrames_.load(std::memory_order_relaxed)) +
                    framesQueued_.load(std::memory_order_relaxed);
            // Filtro: la latencia de MMAP es de pocas ráfagas; una lectura absurda
            // (hardwarePosition atrasado tras un flush) no debe arrastrar el reloj.
            if (measured >= -static_cast<int64_t>(streamRate_) &&
                measured <= std::max<int64_t>(upperBound, streamRate_)) {
                latencyFrames = std::max<int64_t>(0, measured);
                lastLatencyFrames_.store(latencyFrames, std::memory_order_relaxed);

                // Ancla del HAL: se guarda (frames presentados, instante del reloj).
                // `hardwarePosition` es monótono dentro del stream: si retrocede, el HAL
                // re-ancló (o el stream se reinició) y hay que descartar la referencia
                // vieja, no "corregir" el reloj hacia atrás.
                const int64_t anchorFrames =
                        timestampAnchorFrames_.load(std::memory_order_relaxed);
                if (hardwarePosition >= anchorFrames) {
                    timestampAnchorFrames_.store(hardwarePosition, std::memory_order_relaxed);
                    timestampAnchorNanos_.store(hardwareTime, std::memory_order_relaxed);
                } else {
                    logW("timestamp del HAL retrocedió (%lld < %lld): se re-ancla",
                         static_cast<long long>(hardwarePosition),
                         static_cast<long long>(anchorFrames));
                    timestampAnchorFrames_.store(hardwarePosition, std::memory_order_relaxed);
                    timestampAnchorNanos_.store(hardwareTime, std::memory_order_relaxed);
                }
            }
        } else {
            // Entre refrescos del timestamp: se extrapola desde el ancla con el reloj
            // del sistema, que avanza suave, en vez de dejar la posición clavada hasta
            // la próxima ráfaga del HAL (eso es el "paso a pasitos" de la barra).
            const int64_t anchorNanos = timestampAnchorNanos_.load(std::memory_order_relaxed);
            const int64_t now = nowNanos();
            if (anchorNanos > 0 && now > anchorNanos && streamRate_ > 0) {
                const int64_t elapsedFrames =
                        (now - anchorNanos) * static_cast<int64_t>(streamRate_) / 1000000000LL;
                const int64_t presented = timestampAnchorFrames_.load(std::memory_order_relaxed) +
                                          elapsedFrames;
                // Nunca por delante de lo que el callback ya consumió.
                const int64_t clamped = std::min(presented, consumed);
                const int64_t extrapolated = consumed - clamped;
                if (extrapolated >= 0 && extrapolated <=
                        std::max<int64_t>(static_cast<int64_t>(hardwareBufferFrames_.load(
                                                  std::memory_order_relaxed)) * 2,
                                          static_cast<int64_t>(streamRate_) / 10)) {
                    latencyFrames = extrapolated;
                }
            }
        }
    }
    if (forceZeroLatency) latencyFrames = 0;
    int64_t position = consumed - latencyFrames;
    position = std::max<int64_t>(0, std::min(position, consumed));
    int64_t previous = lastPositionFrames_.load(std::memory_order_relaxed);
    // La posición de un mismo stream no puede retroceder: eso confundiría al sink.
    if (position < previous) position = previous;
    lastPositionFrames_.store(position, std::memory_order_relaxed);
}

int64_t AudioEngine::positionFrames() {
    updatePlayoutPosition(false);
    return lastPositionFrames_.load(std::memory_order_relaxed);
}

int64_t AudioEngine::positionMicros() {
    const int64_t frames = positionFrames();
    if (streamRate_ <= 0) return 0;
    // Se devuelve en el dominio temporal del ORIGEN (lo que espera Media3: microsegundos
    // de pista), no en el del stream, para que el resampleo no desplace el reloj.
    const int64_t rate = sourceRate_ > 0 ? sourceRate_ : streamRate_;
    const int64_t sourceFrames = frames * rate / streamRate_;
    return sourceFrames * 1000000LL / rate;
}

int64_t AudioEngine::latencyMicros() {
    if (streamRate_ <= 0) return -1;
    if (!stream_) return -1;
    updatePlayoutPosition(false);
    const int64_t frames = lastLatencyFrames_.load(std::memory_order_relaxed);
    return frames * 1000000LL / streamRate_;
}

int64_t AudioEngine::sourceFramesQueued() const {
    const int64_t queued = framesQueued_.load(std::memory_order_relaxed);
    if (streamRate_ <= 0 || sourceRate_ <= 0 || streamRate_ == sourceRate_) return queued;
    return queued * sourceRate_ / streamRate_;
}

bool AudioEngine::endOfStreamReached() const {
    if (!endOfStream_.load(std::memory_order_acquire)) return false;
    return ring_.availableToRead() == 0;
}

oboe::StreamState AudioEngine::state() const {
    return stream_ ? stream_->getState() : oboe::StreamState::Closed;
}

StreamInfo AudioEngine::info() const {
    StreamInfo info;
    info.api = apiCode_;
    info.sourceFormat = static_cast<int32_t>(sourceFormat_);
    info.sourceSampleRate = sourceRate_;
    info.sourceChannels = sourceChannels_;
    info.volumePercent = static_cast<int32_t>(
            volume_.load(std::memory_order_relaxed) * 100.0f + 0.5f);
    info.format = streamFormat_;
    info.sampleRate = streamRate_;
    info.channels = streamChannels_;
    info.deviceId = stream_ ? stream_->getDeviceId() : deviceId_;
    info.errorCode = errorCode_.load(std::memory_order_relaxed);
    info.latencyMicros = streamRate_ > 0
            ? lastLatencyFrames_.load(std::memory_order_relaxed) * 1000000LL / streamRate_
            : -1;
    info.mmap = mmap_;
    if (stream_) {
        info.framesPerBurst = stream_->getFramesPerBurst();
        info.bufferFrames = stream_->getBufferSizeInFrames();
        info.exclusive = exclusive_;
        info.passthrough = passthrough_;
        // Bit-perfect de verdad: stream exclusivo, misma pista sin conversión y ruta
        // directa (sin fundidos ni volumen digital). Es el mismo criterio que usa el
        // lado Kotlin (`EngineInfo.bitPerfect`), pero evaluado donde importa: el motor.
        info.bitPerfect = exclusive_ && passthrough_ && directPath_ &&
                          volumeApplied_ == 1.0f;
        const auto xRuns = stream_->getXRunCount();
        const int64_t nativeXRuns = xRuns.error() == oboe::Result::OK ? xRuns.value() : 0;
        info.xRuns = static_cast<int32_t>(
                nativeXRuns + underruns_.load(std::memory_order_relaxed));
    }
    return info;
}

size_t AudioEngine::writePacked(const uint8_t* data, size_t byteCount) {
    if (data == nullptr || byteCount == 0) return 0;

    // La apertura del stream corre en su propio hilo: si todavía está en curso se
    // espera un máximo acotado (la primera vez), y si falló se publica el error para
    // que el sink pueda reconstruirse en vez de reintentar para siempre.
    if (openPending_.load(std::memory_order_acquire) && !awaitOpen()) {
        return 0;
    }
    if (!stream_) return 0;

    const size_t accepted = writeToRing(data, byteCount);
    if (startPending_.load(std::memory_order_acquire)) {
        maybeStart();
    }
    return accepted;
}

size_t AudioEngine::writeToRing(const uint8_t* data, size_t byteCount) {
    const size_t frameBytes = streamFrameBytes_ > 0 ? static_cast<size_t>(streamFrameBytes_) : 4;
    // Camino directo (byte a byte, sin decodificar a float ni re-cuantizar). Además de
    // la señal intacta hace falta que ningún fundido esté en curso: un fundido tiene que
    // escalar muestras, y hacerlo en float re-cuantiza el bloque.
    const bool plainCopy = passthrough_ &&
            directPath_ &&
            !fadeRequest_.load(std::memory_order_relaxed) &&
            gain_ == 1.0f &&
            volumeApplied_ == 1.0f &&
            volume_.load(std::memory_order_relaxed) == 1.0f;
    if (plainCopy) {
        // Camino bit-perfect: sólo se aceptan frames completos que ya quepan. Nunca
        // queda una cola parcial en el ring ni se guardan bytes de un bloque anterior.
        const size_t frames = byteCount / frameBytes;
        if (frames == 0) return 0;
        const size_t want = frames * frameBytes;
        const size_t fit = std::min(want, ring_.freeSpace() / frameBytes * frameBytes);
        if (fit == 0) return 0;
        const size_t written = ring_.write(data, fit);
        framesQueued_.fetch_add(static_cast<int64_t>(written / frameBytes),
                                std::memory_order_relaxed);
        return written;
    }

    // Camino con transformación. Regla de oro (la usa `DefaultAudioSink` al contar
    // frames: `bytesWritten = bytesRemaining - buffer.remaining()`): NUNCA hay que
    // consumir entrada que no se pueda procesar y contabilizar ya mismo. Por eso se
    // acepta sólo la entrada cuya salida GARANTIZADA cabe en el ring; el resto se
    // devuelve como no aceptado y Media3 reenvía exactamente ese tramo.
    const int32_t srcFrameBytes = bytesPerSample(sourceFormat_) * sourceChannels_;
    if (srcFrameBytes <= 0) return 0;
    const size_t srcFrameBytesSz = static_cast<size_t>(srcFrameBytes);
    const size_t freeFrames = ring_.freeSpace() / frameBytes;
    // Dos frames de margen: el filtro puede producir hasta un frame extra por
    // bloque por el redondeo de fase.
    if (freeFrames <= 2) return 0;

    size_t inFrames = byteCount / srcFrameBytesSz;
    if (inFrames == 0) return 0;
    // Presupuesto: cuánta entrada cabe (sin y con resampleo). El margen de 2 frames
    // absorbe el frame extra que el filtro puede producir en el borde del bloque.
    const double step = resampler_.bypass() ? 1.0
                                            : (resampler_.step() > 0.0 ? resampler_.step() : 1.0);
    const size_t allowedIn = static_cast<size_t>(
            static_cast<double>(freeFrames - 2) * step);
    if (allowedIn == 0) return 0;
    inFrames = std::min(inFrames, allowedIn);

    const size_t inBytes = inFrames * srcFrameBytesSz;
    const int32_t inSamples = static_cast<int32_t>(inFrames * static_cast<size_t>(sourceChannels_));

    if (srcFloats_.size() < static_cast<size_t>(inSamples)) {
        srcFloats_.resize(static_cast<size_t>(inSamples));
    }
    decodeToFloat(data, inSamples, sourceFormat_, srcFloats_.data());

    // Volumen digital con rampa dentro del bloque (evita el clic al moverlo).
    const float target = targetGain();
    if (target != 1.0f || volumeApplied_ != 1.0f) {
        const float start = volumeApplied_;
        const int32_t framesTotal = static_cast<int32_t>(inFrames);
        for (int32_t i = 0; i < framesTotal; ++i) {
            const float g = framesTotal > 1
                    ? start + (target - start) *
                              (static_cast<float>(i) / static_cast<float>(framesTotal - 1))
                    : target;
            float* frame = srcFloats_.data() + static_cast<size_t>(i) * sourceChannels_;
            for (int32_t c = 0; c < sourceChannels_; ++c) frame[c] *= g;
        }
        volumeApplied_ = target;
    }

    const float* source = srcFloats_.data();
    int32_t frames = static_cast<int32_t>(inFrames);

    // Mapeo de canales si el stream abrió con otro número.
    if (sourceChannels_ != streamChannels_) {
        if (mapped_.size() < static_cast<size_t>(frames) * streamChannels_) {
            mapped_.resize(static_cast<size_t>(frames) * streamChannels_);
        }
        for (int32_t i = 0; i < frames; ++i) {
            const float* s = source + static_cast<size_t>(i) * sourceChannels_;
            float* d = mapped_.data() + static_cast<size_t>(i) * streamChannels_;
            for (int32_t c = 0; c < streamChannels_; ++c) {
                if (c < sourceChannels_) {
                    d[c] = s[c];
                } else if (sourceChannels_ == 1) {
                    d[c] = s[0];       // mono replicado: -3 dB no, misma señal
                } else {
                    d[c] = 0.0f;       // canal que la pista no tiene: silencio, no basura
                }
            }
        }
        source = mapped_.data();
    }

    const float* finalFloat = source;
    int32_t outFrames = frames;
    if (!resampler_.bypass()) {
        const double st = resampler_.step() > 0.0 ? resampler_.step() : 1.0;
        const int32_t capacityFrames =
                static_cast<int32_t>(static_cast<double>(frames + 4 * resampler_.halfFrames() + 8) / st) + 16;
        if (resampled_.size() < static_cast<size_t>(capacityFrames) * streamChannels_) {
            resampled_.resize(static_cast<size_t>(capacityFrames) * streamChannels_);
        }
        outFrames = resampler_.process(source, frames, resampled_.data(), capacityFrames);
        finalFloat = resampled_.data();
    }

    const int32_t outSamples = outFrames * streamChannels_;
    if (outSamples > 0) {
        const size_t outBytes = static_cast<size_t>(outSamples) *
                                static_cast<size_t>(bytesPerSample(
                                        static_cast<PcmFormat>(streamFormat_)));
        if (outBytes_.size() < outBytes) outBytes_.resize(outBytes);
        encodeFromFloat(finalFloat, outSamples, static_cast<PcmFormat>(streamFormat_),
                        outBytes_.data());
        // Longitud LÓGICA del bloque actual (outBytes), nunca `outBytes_.size()`:
        // ese era el bug que encolaba bytes del bloque anterior.
        const size_t written = std::min(outBytes, ring_.freeSpace() / frameBytes * frameBytes);
        const size_t queued = written > 0 ? ring_.write(outBytes_.data(), written) : 0;
        framesQueued_.fetch_add(static_cast<int64_t>(queued / frameBytes),
                                std::memory_order_relaxed);
        if (queued < outBytes) {
            framesDropped_.fetch_add(static_cast<int64_t>((outBytes - queued) / frameBytes),
                                     std::memory_order_relaxed);
        }
    }
    return inBytes;
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* /*stream*/,
                                                   void* audioData,
                                                   int32_t numFrames) {
    auto* out = static_cast<uint8_t*>(audioData);
    const size_t byteCount = static_cast<size_t>(numFrames) *
                             static_cast<size_t>(streamFrameBytes_);

    // 1) Peticiones de fundido del hilo de control.
    const int32_t request = fadeRequest_.exchange(kFadeNone, std::memory_order_acquire);
    switch (request) {
        case kFadeIn:
            holding_.store(false, std::memory_order_relaxed);
            if (fades_.load(std::memory_order_relaxed) && gain_ < 1.0f) {
                const int32_t frames = fadeInFrames_.load(std::memory_order_relaxed);
                gainStep_ = (1.0f - gain_) / static_cast<float>(frames > 0 ? frames : 1);
            } else {
                gain_ = 1.0f;
                gainStep_ = 0.0f;
            }
            break;
        case kFadeOutAndHold:
            if (fades_.load(std::memory_order_relaxed) && gain_ > 0.0f) {
                const int32_t frames = fadeOutFrames_.load(std::memory_order_relaxed);
                gainStep_ = -gain_ / static_cast<float>(frames > 0 ? frames : 1);
                stopAfterFade_ = true;
            } else {
                // Sin fundido pendiente: se descarta el resto y se para.
                gain_ = 0.0f;
                gainStep_ = 0.0f;
                holding_.store(true, std::memory_order_relaxed);
                return oboe::DataCallbackResult::Stop;
            }
            break;
        case kFadeRearm:
            if (fades_.load(std::memory_order_relaxed)) {
                gain_ = 0.0f;
                const int32_t frames = rearmFrames_.load(std::memory_order_relaxed);
                gainStep_ = 1.0f / static_cast<float>(frames > 0 ? frames : 1);
            }
            break;
        default:
            break;
    }

    // 2) Guardia de parada: si el callback ya había parado (Stop) y quedó audio
    //    encolado, se reanuda con Start. Si no hay datos nuevos, se devuelve Stop (sin
    //    memcpy: el HAL no consume ese buffer) y no se gasta CPU en silencio.
    if (holding_.load(std::memory_order_relaxed)) {
        if (!endOfStream_.load(std::memory_order_acquire) &&
            ring_.availableToRead() >= static_cast<size_t>(streamFrameBytes_) &&
            !startPending_.load(std::memory_order_acquire)) {
            holding_.store(false, std::memory_order_relaxed);
            if (fades_.load(std::memory_order_relaxed) && gain_ < 1.0f) {
                const int32_t frames = fadeInFrames_.load(std::memory_order_relaxed);
                gainStep_ = (1.0f - gain_) / static_cast<float>(frames > 0 ? frames : 1);
            }
        } else {
            std::memset(out, 0, byteCount);
            return oboe::DataCallbackResult::Stop;
        }
    }

    // 3) Copia pura (sin ganancia) salvo durante una rampa de unos pocos milisegundos.
    const size_t got = ring_.read(out, byteCount);
    if (got < byteCount) {
        std::memset(out + got, 0, byteCount - got);
        if (!stopAfterFade_) underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    const int32_t gotFrames = static_cast<int32_t>(got / static_cast<size_t>(streamFrameBytes_));
    if (gotFrames > 0 && (gain_ != 1.0f || gainStep_ != 0.0f)) {
        applyGainRamp(out, gotFrames);
    }
    framesConsumed_.fetch_add(std::max(0, gotFrames), std::memory_order_relaxed);

    // 4) Parada decidida por el callback, que es el único que sabe cuánto falta.
    //    - En pausa (`stopAfterFade_`): se para al terminar el fade, sin importar
    //      cuánto quede en el anillo (esos datos se guardan para reanudar).
    //    - Fin de stream (Media3 llamó a stop()): se vacía TODO el anillo y se para
    //      recién cuando no quedan bytes. Ese es el contrato de `AudioOutput.stop()`.
    const bool ringEmpty = ring_.availableToRead() == 0;
    if (stopAfterFade_) {
        if (gain_ <= 0.0f || ringEmpty) {
            gain_ = 0.0f;
            gainStep_ = 0.0f;
            stopAfterFade_ = false;
            holding_.store(true, std::memory_order_relaxed);
            return oboe::DataCallbackResult::Stop;
        }
        return oboe::DataCallbackResult::Continue;
    }
    if (ringEmpty) {
        // No queda audio: se para siempre (aunque no haya llegado stop() todavía).
        // Con datos nuevos, el próximo bloque usa DataCallbackResult::Start, así que
        // esto no rompe el gapless: la parada es "sin datos", no "fin de stream".
        holding_.store(true, std::memory_order_relaxed);
        return oboe::DataCallbackResult::Stop;
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::applyGainRamp(uint8_t* data, int32_t frames) {
    const int32_t ch = streamChannels_;
    if (data == nullptr || frames <= 0 || ch <= 0) return;

    const float step = gainStep_;
    const bool up = step > 0.0f;
    float g = gain_;

    const auto advance = [&]() {
        g += step;
        if (up && g > 1.0f) g = 1.0f;
        if (!up && g < 0.0f) g = 0.0f;
    };

    switch (static_cast<PcmFormat>(streamFormat_)) {
        case PcmFormat::kI16: {
            auto* samples = reinterpret_cast<int16_t*>(data);
            for (int32_t f = 0; f < frames; ++f) {
                if (g == 1.0f) break;
                int16_t* frame = samples + static_cast<size_t>(f) * ch;
                for (int32_t c = 0; c < ch; ++c) {
                    frame[c] = static_cast<int16_t>(std::lrint(static_cast<float>(frame[c]) * g));
                }
                advance();
            }
            break;
        }
        case PcmFormat::kF32: {
            auto* samples = reinterpret_cast<float*>(data);
            for (int32_t f = 0; f < frames; ++f) {
                if (g == 1.0f) break;
                float* frame = samples + static_cast<size_t>(f) * ch;
                for (int32_t c = 0; c < ch; ++c) frame[c] *= g;
                advance();
            }
            break;
        }
        case PcmFormat::kI32: {
            auto* samples = reinterpret_cast<int32_t*>(data);
            for (int32_t f = 0; f < frames; ++f) {
                if (g == 1.0f) break;
                int32_t* frame = samples + static_cast<size_t>(f) * ch;
                for (int32_t c = 0; c < ch; ++c) {
                    double v = static_cast<double>(frame[c]) * static_cast<double>(g);
                    if (v > 2147483647.0) v = 2147483647.0;
                    if (v < -2147483648.0) v = -2147483648.0;
                    frame[c] = static_cast<int32_t>(std::lrint(v));
                }
                advance();
            }
            break;
        }
        case PcmFormat::kI24: {
            for (int32_t f = 0; f < frames; ++f) {
                if (g == 1.0f) break;
                uint8_t* frame = data + static_cast<size_t>(f) * ch * 3;
                for (int32_t c = 0; c < ch; ++c) {
                    uint8_t* b = frame + static_cast<size_t>(c) * 3;
                    int32_t v = static_cast<int32_t>(
                            static_cast<uint32_t>(b[0]) |
                            (static_cast<uint32_t>(b[1]) << 8) |
                            (static_cast<uint32_t>(b[2]) << 16));
                    if (v & 0x800000) v |= ~0xFFFFFF;   // extender signo de 24 bits
                    double scaled = static_cast<double>(v) * static_cast<double>(g);
                    if (scaled > 8388607.0) scaled = 8388607.0;
                    if (scaled < -8388608.0) scaled = -8388608.0;
                    const int32_t s24 = static_cast<int32_t>(std::lrint(scaled)) & 0xFFFFFF;
                    b[0] = static_cast<uint8_t>(s24 & 0xFF);
                    b[1] = static_cast<uint8_t>((s24 >> 8) & 0xFF);
                    b[2] = static_cast<uint8_t>((s24 >> 16) & 0xFF);
                }
                advance();
            }
            break;
        }
    }

    gain_ = g;
    if ((up && gain_ >= 1.0f) || (!up && gain_ <= 0.0f)) gainStep_ = 0.0f;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    logE("stream error after close: %s (posible DAC desconectado)", oboe::convertToText(error));
    errorCode_.store(static_cast<int32_t>(error), std::memory_order_relaxed);
    holding_.store(true, std::memory_order_relaxed);
    startPending_.store(false, std::memory_order_relaxed);
}

} // namespace audio

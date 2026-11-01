#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdarg>

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

constexpr int32_t kPhases = 512;
constexpr double kKaiserBeta = 8.0;
constexpr int32_t kBaseHalf = 12;

inline float clamp1(double x) {
    return static_cast<float>(x < -1.0 ? -1.0 : (x > 1.0 ? 1.0 : x));
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
                const uint8_t* p = src + i * 3;
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
                dst[i] = static_cast<float>(s[i] / 2147483648.0);
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
                uint8_t* p = dst + i * 3;
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
    const double fc = step_ > 1.0 ? 0.5 / step_ : 0.5;
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
                kernel_[static_cast<size_t>(ph) * taps_ + t] =
                        static_cast<float>(kernel_[static_cast<size_t>(ph) * taps_ + t] / sum);
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

    // Compactar conservando la ventana que necesita la próxima llamada.
    const int32_t keepFrom = std::max(0, static_cast<int32_t>(std::floor(pos_)) - half_ + 1);
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
    builder.setFormat(static_cast<oboe::AudioFormat>(
            format == PcmFormat::kI16 ? oboe::AudioFormat::I16
            : format == PcmFormat::kI24 ? oboe::AudioFormat::I24
            : format == PcmFormat::kI32 ? oboe::AudioFormat::I32
                                        : oboe::AudioFormat::Float));
    builder.setSampleRate(sampleRate > 0 ? sampleRate : 48000);
    builder.setChannelCount(channels > 0 ? channels : oboe::ChannelCount::Stereo);
    builder.setSharingMode(sharing);
    builder.setPerformanceMode(performance);
    builder.setUsage(oboe::Usage::Media);
    builder.setContentType(oboe::ContentType::Music);
    builder.setSpatializationBehavior(oboe::SpatializationBehavior::Never);
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
    const oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        stream_.reset();
    }
    return result;
}

oboe::Result AudioEngine::open(const EngineConfig& config) {
    close();

    sourceFormat_ = config.sourceFormat;
    sourceChannels_ = config.sourceChannels > 0 ? config.sourceChannels : 2;
    sourceRate_ = config.sourceSampleRate > 0 ? config.sourceSampleRate : 48000;
    deviceId_ = config.deviceId;
    errorCode_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    streamFramesConsumed_.store(0, std::memory_order_relaxed);

    logI("open: source=%s %d Hz %d ch strict=%d exclusiveOnly=%d buffer=%dms device=%d",
         formatName(sourceFormat_), sourceRate_, sourceChannels_,
         config.strictBitPerfect ? 1 : 0, config.requireExclusive ? 1 : 0,
         config.bufferMs, deviceId_);

    // 1) Camino bit-perfect: mismo formato, mismo rate, exclusivo, sin fast mixer.
    oboe::Result result = openStream(sourceFormat_, sourceRate_, sourceChannels_,
                                     oboe::SharingMode::Exclusive,
                                     oboe::PerformanceMode::None);
    if (result != oboe::Result::OK) {
        logW("exclusivo %s @ %d falló: %s", formatName(sourceFormat_), sourceRate_,
             oboe::convertToText(result));
    }

    // 2) Mismo formato/rate pero compartido (el HAL puede resamplear: se informa).
    //    Los modos exclusivos no aceptan este paso: si el HAL no da exclusivo, se avisa.
    if (result != oboe::Result::OK && !config.requireExclusive) {
        result = openStream(sourceFormat_, sourceRate_, sourceChannels_,
                            oboe::SharingMode::Shared,
                            oboe::PerformanceMode::None);
        if (result != oboe::Result::OK) {
            logW("compartido %s @ %d falló: %s", formatName(sourceFormat_), sourceRate_,
                 oboe::convertToText(result));
        }
    }

    // 3) Fallback: conversión propia (Kaiser) a un formato que el equipo acepte.
    if (result != oboe::Result::OK && !config.strictBitPerfect) {
        const int32_t rates[] = {48000, 44100, 96000, 192000, 88200, 176400};
        const PcmFormat formats[] = {PcmFormat::kF32, PcmFormat::kI16, PcmFormat::kI32};
        for (int32_t i = 0; i < 6 && result != oboe::Result::OK; ++i) {
            for (int32_t f = 0; f < 3 && result != oboe::Result::OK; ++f) {
                if (rates[i] == sourceRate_ && formats[f] == sourceFormat_) continue;
                result = openStream(formats[f], rates[i], sourceChannels_,
                                    config.requireExclusive ? oboe::SharingMode::Exclusive
                                                            : oboe::SharingMode::Shared,
                                    oboe::PerformanceMode::LowLatency);
                if (result == oboe::Result::OK) {
                    logI("fallback aceptado: %s @ %d (fuente %s @ %d)",
                         formatName(formats[f]), rates[i], formatName(sourceFormat_), sourceRate_);
                }
            }
        }
    }

    if (result != oboe::Result::OK) {
        logE("no se pudo abrir ningún stream: %s", oboe::convertToText(result));
        errorCode_.store(static_cast<int32_t>(result), std::memory_order_relaxed);
        return result;
    }

    finalizeOpen(config);
    return oboe::Result::OK;
}

void AudioEngine::finalizeOpen(const EngineConfig& config) {
    streamFormat_ = static_cast<int32_t>(stream_->getFormat() == oboe::AudioFormat::I16 ? PcmFormat::kI16
                     : stream_->getFormat() == oboe::AudioFormat::I24 ? PcmFormat::kI24
                     : stream_->getFormat() == oboe::AudioFormat::I32 ? PcmFormat::kI32
                                                                     : PcmFormat::kF32);
    streamRate_ = stream_->getSampleRate();
    streamChannels_ = stream_->getChannelCount();
    streamFrameBytes_ = streamChannels_ * bytesPerSample(static_cast<PcmFormat>(streamFormat_));
    if (streamFrameBytes_ <= 0) streamFrameBytes_ = 4;
    apiCode_ = stream_->getAudioApi() == oboe::AudioApi::AAudio ? 0 : 1;
    exclusive_ = stream_->getSharingMode() == oboe::SharingMode::Exclusive;

    passthrough_ = static_cast<PcmFormat>(streamFormat_) == sourceFormat_ &&
                   streamRate_ == sourceRate_ &&
                   streamChannels_ == sourceChannels_;

    resampler_.configure(sourceRate_, streamRate_, streamChannels_);
    volumeApplied_ = volume_.load(std::memory_order_relaxed);

    // Fundidos: 25 ms de entrada y 60 ms de salida. Con fades apagados la señal sale
    // intacta desde el primer frame (modo purista).
    fades_ = config.fades;
    fadeInFrames_ = fades_ ? std::max(24, streamRate_ * 25 / 1000) : 0;
    fadeOutFrames_ = fades_ ? std::max(48, streamRate_ * 60 / 1000) : 0;
    rearmFrames_ = fades_ ? std::max(16, streamRate_ * 12 / 1000) : 0;
    gain_ = fades_ ? 0.0f : 1.0f;
    gainStep_ = 0.0f;
    stopAfterFade_ = false;
    fadeRequest_.store(kFadeNone, std::memory_order_relaxed);
    holding_.store(false, std::memory_order_relaxed);

    // Reparto de buffers, que es lo que define la velocidad percibida:
    //  - ABAJO (AAudio) el buffer más chico posible: 2 ráfagas. Es lo que se tira
    //    cuando hay un salto y lo que hay que rellenar después; si fuera de 250 ms,
    //    cada skip se sentiría como un microsilencio.
    //  - ARRIBA (nuestro anillo) el buffer grande del ajuste "Buffer de salida": ahí
    //    vive la estabilidad, y limpiarlo es instantáneo porque es memoria nuestra.
    const int32_t burst = stream_->getFramesPerBurst() > 0 ? stream_->getFramesPerBurst() : 192;
    const int32_t capacity = stream_->getBufferCapacityInFrames();
    // Exclusivo (MMAP): 2 ráfagas, el mínimo recomendado por Oboe y lo más rápido de
    // vaciar. Compartido: 3, porque ahí abajo hay un mezclador que ya suma latencia y
    // conviene un poco de colchón.
    int32_t hwFrames = std::max(exclusive_ ? burst * 2 : burst * 3, burst);
    if (capacity > 0) hwFrames = std::min(capacity, hwFrames);
    if (hwFrames > 0) {
        stream_->setBufferSizeInFrames(hwFrames);
    }
    hardwareBufferFrames_ = hwFrames;

    // Cuánto audio hace falta para arrancar el stream: dos ráfagas o 20 ms, lo que
    // sea mayor. Arrancar con el anillo vacío sólo produce silencio y xruns.
    const int32_t targetBufferMs = std::max(40, config.bufferMs);
    prerollFrames_ = std::max(burst * 2, streamRate_ * 20 / 1000);
    startPending_.store(false, std::memory_order_relaxed);

    // Ring: 4 x buffer, mínimo 256 KB, máximo 4 MB.
    const size_t bytesPerSecond = static_cast<size_t>(streamRate_) *
                                  static_cast<size_t>(streamFrameBytes_);
    size_t ringBytes = bytesPerSecond * static_cast<size_t>(std::max(targetBufferMs, 200)) / 1000 * 4;
    ringBytes = std::max<size_t>(ringBytes, 1u << 18);
    ringBytes = std::min<size_t>(ringBytes, 1u << 22);
    ring_.resize(ringBytes);
    pending_.clear();

    logI("opened: api=%d format=%s rate=%d ch=%d burst=%d hwBuffer=%d ring=%zuKB preroll=%d "
         "exclusive=%d passthrough=%d",
         apiCode_, formatName(static_cast<PcmFormat>(streamFormat_)), streamRate_,
         streamChannels_, burst, hardwareBufferFrames_, ringBytes / 1024, prerollFrames_,
         exclusive_ ? 1 : 0, passthrough_ ? 1 : 0);
}

oboe::Result AudioEngine::close() {
    if (stream_) {
        stream_->close();
        stream_.reset();
    }
    ring_.clear();
    pending_.clear();
    srcFloats_.clear();
    resampled_.clear();
    mapped_.clear();
    outBytes_.clear();
    apiCode_ = -1;
    streamFormat_ = -1;
    streamRate_ = 0;
    streamChannels_ = 0;
    passthrough_ = true;
    fadeRequest_.store(kFadeNone, std::memory_order_relaxed);
    holding_.store(true, std::memory_order_relaxed);
    gain_ = 1.0f;
    gainStep_ = 0.0f;
    stopAfterFade_ = false;
    return oboe::Result::OK;
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

oboe::Result AudioEngine::start() {
    // El fundido de entrada lo aplica el callback: acá sólo se pide.
    fadeRequest_.store(kFadeIn, std::memory_order_release);
    holding_.store(false, std::memory_order_release);
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
            static_cast<size_t>(prerollFrames_) * static_cast<size_t>(streamFrameBytes_);
    if (needBytes > 0 && ring_.availableToRead() < needBytes) {
        return oboe::Result::OK;   // todavía no hay datos: se reintenta desde writePacked
    }
    startPending_.store(false, std::memory_order_release);
    return setStreamState(0);
}

oboe::Result AudioEngine::pause() {
    startPending_.store(false, std::memory_order_release);
    if (!stream_) return oboe::Result::ErrorInvalidState;
    if (stream_->getState() != oboe::StreamState::Started) {
        // Ya está parado: no hay nada que fundir.
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return oboe::Result::OK;
    }
    if (!fades_) {
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return setStreamState(1);
    }
    // 60 ms de rampa y el callback se detiene solo (DataCallbackResult::Stop).
    fadeRequest_.store(kFadeOutAndStop, std::memory_order_release);
    return oboe::Result::OK;
}

oboe::Result AudioEngine::stop() {
    startPending_.store(false, std::memory_order_release);
    if (!stream_) return oboe::Result::ErrorInvalidState;
    if (stream_->getState() != oboe::StreamState::Started) {
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return oboe::Result::OK;
    }
    if (!fades_) {
        fadeRequest_.store(kFadeNone, std::memory_order_release);
        return setStreamState(2);
    }
    fadeRequest_.store(kFadeOutAndStop, std::memory_order_release);
    return oboe::Result::OK;
}

oboe::Result AudioEngine::flush() {
    ring_.clear();
    pending_.clear();
    resampler_.reset();
    streamFramesConsumed_.store(0, std::memory_order_relaxed);
    // Salto o cambio de pista: el contenido nuevo entra con un fundido corto en vez
    // de aparecer a full en medio de una muestra.
    fadeRequest_.store(fades_ ? kFadeRearm : kFadeNone, std::memory_order_release);
    if (stream_ && stream_->getState() != oboe::StreamState::Started) {
        return oboe::Result::OK;
    }
    return setStreamState(3);
}

oboe::StreamState AudioEngine::state() const {
    return stream_ ? stream_->getState() : oboe::StreamState::Closed;
}

int64_t AudioEngine::sourceFramesConsumed() const {
    const int64_t streamFrames = streamFramesConsumed_.load(std::memory_order_relaxed);
    if (streamRate_ <= 0 || sourceRate_ <= 0 || streamRate_ == sourceRate_) return streamFrames;
    return static_cast<int64_t>(static_cast<double>(streamFrames) *
                                static_cast<double>(sourceRate_) / static_cast<double>(streamRate_));
}

void AudioEngine::setVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_.store(volume, std::memory_order_relaxed);
}

StreamInfo AudioEngine::info() const {
    StreamInfo info;
    info.api = apiCode_;
    info.sourceFormat = static_cast<int32_t>(sourceFormat_);
    info.sourceSampleRate = sourceRate_;
    info.sourceChannels = sourceChannels_;
    info.volumePercent = static_cast<int32_t>(volume_.load(std::memory_order_relaxed) * 100.0f + 0.5f);
    info.format = streamFormat_;
    info.sampleRate = streamRate_;
    info.channels = streamChannels_;
    info.deviceId = stream_ ? stream_->getDeviceId() : deviceId_;
    info.errorCode = errorCode_.load(std::memory_order_relaxed);
    if (stream_) {
        info.framesPerBurst = stream_->getFramesPerBurst();
        info.bufferFrames = stream_->getBufferSizeInFrames();
        info.exclusive = exclusive_;
        info.passthrough = passthrough_;
        info.bitPerfect = exclusive_ && passthrough_;
        const auto xRuns = stream_->getXRunCount();
        const int64_t nativeXRuns = xRuns.error() == oboe::Result::OK ? xRuns.value() : 0;
        info.xRuns = static_cast<int32_t>(
                nativeXRuns + underruns_.load(std::memory_order_relaxed));
    }
    return info;
}

size_t AudioEngine::writePacked(const uint8_t* data, size_t byteCount) {
    if (data == nullptr || byteCount == 0 || !stream_) return 0;

    // Si el play quedó esperando datos, este es el momento de arrancar.
    const size_t accepted = writeToRing(data, byteCount);
    if (startPending_.load(std::memory_order_acquire)) {
        maybeStart();
    }
    return accepted;
}

size_t AudioEngine::writeToRing(const uint8_t* data, size_t byteCount) {
    // Si quedó cola del bloque anterior, primero hay que colocarla.
    if (!pending_.empty()) {
        const size_t n = ring_.write(pending_.data(), pending_.size());
        if (n > 0) pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(n));
        if (!pending_.empty()) return 0;
    }

    if (passthrough_ && volumeApplied_ == 1.0f && volume_.load(std::memory_order_relaxed) == 1.0f) {
        return ring_.write(data, byteCount);
    }
    return writeConverted(data, byteCount);
}

size_t AudioEngine::writeConverted(const uint8_t* data, size_t byteCount) {
    const int32_t srcFrameBytes = bytesPerSample(sourceFormat_) * sourceChannels_;
    if (srcFrameBytes <= 0) return 0;
    const int32_t inFrames = static_cast<int32_t>(byteCount / static_cast<size_t>(srcFrameBytes));
    if (inFrames <= 0) return 0;
    const int32_t inSamples = inFrames * sourceChannels_;

    if (srcFloats_.size() < static_cast<size_t>(inSamples)) {
        srcFloats_.resize(static_cast<size_t>(inSamples));
    }
    decodeToFloat(data, inSamples, sourceFormat_, srcFloats_.data());

    // Volumen digital con rampa dentro del bloque (evita el clic al moverlo).
    const float target = volume_.load(std::memory_order_relaxed);
    if (target != 1.0f || volumeApplied_ != 1.0f) {
        const float start = volumeApplied_;
        const int32_t framesTotal = inFrames;
        for (int32_t i = 0; i < framesTotal; ++i) {
            const float g = framesTotal > 1
                    ? start + (target - start) * (static_cast<float>(i) / static_cast<float>(framesTotal - 1))
                    : target;
            float* frame = srcFloats_.data() + static_cast<size_t>(i) * sourceChannels_;
            for (int32_t c = 0; c < sourceChannels_; ++c) frame[c] *= g;
        }
        volumeApplied_ = target;
    }

    const float* source = srcFloats_.data();
    int32_t frames = inFrames;

    // Mapeo de canales si el stream abrió con otro número (raro pero posible).
    if (sourceChannels_ != streamChannels_) {
        if (mapped_.size() < static_cast<size_t>(frames * streamChannels_)) {
            mapped_.resize(static_cast<size_t>(frames * streamChannels_));
        }
        for (int32_t i = 0; i < frames; ++i) {
            const float* s = source + static_cast<size_t>(i) * sourceChannels_;
            float* d = mapped_.data() + static_cast<size_t>(i) * streamChannels_;
            for (int32_t c = 0; c < streamChannels_; ++c) {
                d[c] = c < sourceChannels_
                        ? s[c]
                        : (sourceChannels_ > 0 ? s[c % sourceChannels_] : 0.0f);
            }
        }
        source = mapped_.data();
    }

    const float* finalFloat = source;
    if (!resampler_.bypass()) {
        const double st = resampler_.step() > 0.0 ? resampler_.step() : 1.0;
        const int32_t capacityFrames =
                static_cast<int32_t>(static_cast<double>(frames + 4 * resampler_.halfFrames() + 8) / st) + 16;
        if (resampled_.size() < static_cast<size_t>(capacityFrames) * streamChannels_) {
            resampled_.resize(static_cast<size_t>(capacityFrames) * streamChannels_);
        }
        const int32_t outFrames = resampler_.process(source, frames, resampled_.data(), capacityFrames);
        if (outFrames <= 0) return byteCount;
        finalFloat = resampled_.data();
        frames = outFrames;
    }

    const int32_t outSamples = frames * streamChannels_;
    const size_t outBytes = static_cast<size_t>(outSamples) * bytesPerSample(static_cast<PcmFormat>(streamFormat_));
    if (outBytes_.size() < outBytes) outBytes_.resize(outBytes);
    encodeFromFloat(finalFloat, outSamples, static_cast<PcmFormat>(streamFormat_), outBytes_.data());

    const size_t written = ring_.write(outBytes_.data(), outBytes);
    if (written < outBytes) {
        pending_.assign(outBytes_.begin() + static_cast<long>(written), outBytes_.end());
    }
    return byteCount;
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
            if (fades_ && gain_ < 1.0f) {
                gainStep_ = (1.0f - gain_) / static_cast<float>(fadeInFrames_ > 0 ? fadeInFrames_ : 1);
            } else {
                gain_ = 1.0f;
                gainStep_ = 0.0f;
            }
            break;
        case kFadeOutAndStop:
            if (fades_ && gain_ > 0.0f) {
                gainStep_ = -gain_ / static_cast<float>(fadeOutFrames_ > 0 ? fadeOutFrames_ : 1);
                stopAfterFade_ = true;
            } else {
                // Sin fundido pendiente: se para de una.
                gain_ = 0.0f;
                gainStep_ = 0.0f;
                holding_.store(true, std::memory_order_relaxed);
                return oboe::DataCallbackResult::Stop;
            }
            break;
        case kFadeRearm:
            // Salto / cambio de pista: entra suave aunque el stream sea el mismo.
            if (fades_) {
                // Salto: fundido corto (12 ms), que se percibe como continuo y tapa
                // el click del corte sin agregar latencia.
                gain_ = 0.0f;
                gainStep_ = 1.0f / static_cast<float>(rearmFrames_ > 0 ? rearmFrames_ : 1);
            }
            break;
        default:
            break;
    }

    // 2) En pausa (después del fundido) no se consume nada: el remanente del anillo
    //    sigue ahí para cuando se reanude.
    if (holding_.load(std::memory_order_relaxed)) {
        std::memset(out, 0, byteCount);
        return oboe::DataCallbackResult::Continue;
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
    streamFramesConsumed_.fetch_add(std::max(0, gotFrames), std::memory_order_relaxed);

    // 4) Fundido de salida terminado (o ya no queda nada que fundir): la parada la
    //    pide el propio callback, que es el único que puede hacerlo en tiempo real.
    if (stopAfterFade_ && (gain_ <= 0.0f || gotFrames <= 0)) {
        gain_ = 0.0f;
        gainStep_ = 0.0f;
        stopAfterFade_ = false;
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

    // Rampa lineal. En cuanto la ganancia vuelve a 1.0 el resto del bloque queda
    // intacto: así el estado estacionario sigue siendo bit-exacto.
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
}

} // namespace audio

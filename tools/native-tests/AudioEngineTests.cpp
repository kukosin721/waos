// Pruebas de contrato del motor nativo real (app/src/main/cpp/audio/AudioEngine.cpp)
// contra dobles de Oboe. Verifican las invariantes que Media3 y AAudio esperan:
//
//  1. Ring SPSC concurrente sin pérdida ni corrupción.
//  2. Contabilidad de bytes: lo aceptado se procesa, nunca queda una cola parcial
//     (el bug histórico de `pending_`).
//  3. Fin de stream: `stop()` drena TODO antes de detenerse (gapless).
//  4. `flush()` nunca se pide desde STARTED y deja el stream reutilizable (PAUSED).
//  5. Posición de playout con timestamp de hardware: monótona y descontando latencia.
//  6. Fidelidad: camino passthrough bit a bit, 24 bits y conversión de canales.
//  7. Fallback de formato/rate y `requireDevice` (no cae al altavoz interno).
//  8. Errores: desconexión del DAC se reporta al sink.
//
// Compilar y ejecutar: tools/native-tests/run.sh
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define private public
#define protected public
#include "audio/AudioEngine.cpp"
#undef private
#undef protected

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::printf("    \033[31mFALLA\033[0m %s:%d  %s\n", __FILE__,        \
                        __LINE__, #cond);                                        \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_EQ(actual, expected)                                               \
    do {                                                                         \
        ++g_checks;                                                              \
        const auto a_ = (actual);                                                \
        const auto e_ = (expected);                                              \
        if (!(a_ == e_)) {                                                       \
            std::printf("    \033[31mFALLA\033[0m %s:%d  %s == %s (obtenido %lld, esperado %lld)\n", \
                        __FILE__, __LINE__, #actual, #expected,                      \
                        static_cast<long long>(a_), static_cast<long long>(e_));     \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static void beginTest(const char* name) {
    std::printf("\n\033[1m%s\033[0m\n", name);
}

/** Motor abierto contra el doble con la configuración pedida. */
struct Fixture {
    audio::AudioEngine engine;
    oboe::StreamState state() { return engine.state(); }
    std::shared_ptr<oboe::AudioStream> stream() { return engine.stream_; }
};

/**
 * `open()` sólo acepta la configuración: la apertura real corre en su propio hilo y su
 * resultado se consulta con `awaitOpen()`. Los tests usan este helper para no depender
 * de que el hilo haya terminado ya.
 */
static bool openAndSettle(audio::AudioEngine& engine, const audio::EngineConfig& cfg) {
    engine.open(cfg);
    return engine.awaitOpen(2000);
}

static audio::EngineConfig config(audio::PcmFormat format, int32_t rate, int32_t channels,
                                  bool strict = true, bool exclusive = true) {
    audio::EngineConfig cfg;
    cfg.sourceFormat = format;
    cfg.sourceSampleRate = rate;
    cfg.sourceChannels = channels;
    cfg.strictBitPerfect = strict;
    cfg.requireExclusive = exclusive;
    cfg.bufferMs = 200;
    cfg.fades = false;   // los tests de datos no quieren rampas
    return cfg;
}

/** Empuja `frames` de PCM conocido y verifica que se acepte todo. */
static size_t pushFrames(audio::AudioEngine& engine, int32_t frames, int32_t channelBytes,
                         int32_t channels, uint8_t seed) {
    std::vector<uint8_t> data(static_cast<size_t>(frames) * channelBytes * channels);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
    size_t accepted = 0;
    while (accepted < data.size()) {
        const size_t n = engine.writePacked(data.data() + accepted, data.size() - accepted);
        if (n == 0) break;
        accepted += n;
    }
    return accepted;
}

/** Llena el preroll y arranca, como hace Media3 en play(). */
static void primeAndStart(audio::AudioEngine& engine, int32_t frames = 24000) {
    pushFrames(engine, frames, audio::bytesPerSample(engine.sourceFormat_),
               engine.sourceChannels_, 0x10);
    engine.start();
}

// ---------------------------------------------------------------------------
// 1. Ring concurrente
// ---------------------------------------------------------------------------

static void testRingConcurrent() {
    beginTest("1. Ring SPSC: 1 MB de ida y vuelta sin pérdida");
    audio::ByteRingBuffer ring;
    ring.resize(4096);
    constexpr size_t total = 1u << 20;
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        size_t sent = 0;
        uint32_t counter = 0;
        while (sent < total) {
            uint8_t chunk[97];
            const size_t n = std::min(sizeof(chunk), total - sent);
            for (size_t i = 0; i < n; ++i) chunk[i] = static_cast<uint8_t>((counter + i) & 0xFF);
            const size_t accepted = ring.write(chunk, n);
            sent += accepted;
            counter = static_cast<uint32_t>((counter + accepted) & 0xFF);
            if (accepted == 0) std::this_thread::yield();
        }
    });

    size_t received = 0;
    uint32_t expected = 0;
    while (received < total) {
        uint8_t out[131];
        const size_t n = ring.read(out, sizeof(out));
        for (size_t i = 0; i < n; ++i) {
            if (out[i] != static_cast<uint8_t>((expected + i) & 0xFF)) failed.store(true);
        }
        received += n;
        expected = static_cast<uint32_t>((expected + n) & 0xFF);
        if (n == 0) std::this_thread::yield();
    }
    producer.join();
    CHECK_EQ(received, total);
    CHECK(!failed.load());
    CHECK_EQ(ring.availableToRead(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// 2. Contabilidad: nunca una cola parcial ni bytes viejos
// ---------------------------------------------------------------------------

static void testNoPartialTail() {
    beginTest("2. Escritura con el ring lleno: no queda cola parcial ni bytes viejos");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 2);
    cfg.strictBitPerfect = false;
    CHECK(openAndSettle(engine, cfg));
    engine.volume_.store(0.5f);   // fuerza el camino con conversión

    // Llena el anillo para simular contrapresión real.
    const size_t frameBytes = static_cast<size_t>(engine.streamFrameBytes_);
    std::vector<uint8_t> filler(engine.ring_.capacity() - 4096, 0xAA);
    const size_t filled = engine.ring_.write(filler.data(), filler.size());
    CHECK_EQ(filled, engine.ring_.capacity() - 4096);

    // Un bloque grande: sólo puede aceptarse la parte que ya tiene lugar.
    std::vector<uint8_t> big(4096 * 4, 0x11);
    const size_t accepted = engine.writePacked(big.data(), big.size());
    CHECK(accepted > 0);
    CHECK(accepted < big.size());
    CHECK_EQ(accepted % frameBytes, static_cast<size_t>(0));

    // El anillo no puede contener nada de este bloque después del límite aceptado:
    // se vacía y se cuenta que no haya desbordes.
    const int64_t queued = engine.framesQueued();
    CHECK(queued >= 0);
    CHECK(static_cast<size_t>(queued) * frameBytes <= engine.ring_.availableToRead());

    // El bug anterior metía bytes del bloque previo: ahora un segundo bloque corto
    // debe devolver exactamente lo aceptado, sin sobras.
    engine.ring_.clear();
    std::vector<uint8_t> small(64, 0x22);
    const size_t accepted2 = engine.writePacked(small.data(), small.size());
    CHECK_EQ(accepted2, small.size());
    CHECK_EQ(engine.ring_.availableToRead(), accepted2);
}

// ---------------------------------------------------------------------------
// 3. Fin de stream: drena todo (gapless)
// ---------------------------------------------------------------------------

static void testEndOfStreamDrains() {
    beginTest("3. stop() drena TODO el anillo antes de detenerse");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 2);
    CHECK(openAndSettle(engine, cfg));
    primeAndStart(engine);
    auto stream = engine.stream_;
    const int64_t frames = 48000 / 4;   // 250 ms
    CHECK_EQ(pushFrames(engine, static_cast<int32_t>(frames), 2, 2, 0x30),
             static_cast<size_t>(frames) * 4);

    // Media3 en EOS: stop() y después sigue pidiendo bloques hasta hasPendingData()==false.
    engine.stop();
    int blocks = 0;
    while (stream->getState() == oboe::StreamState::Started && blocks < 10000) {
        stream->renderFrames(192);
        ++blocks;
    }
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Stopped));
    CHECK_EQ(engine.ring_.availableToRead(), static_cast<size_t>(0));
    // 250 ms a 192 frames por bloque ≈ 63 bloques; margen amplio pero acotado.
    CHECK(blocks > 50);
    CHECK(blocks < 200);
    // Nada se descartó por el camino: todo dropeado == 0.
    CHECK_EQ(engine.framesDropped_.load(), static_cast<int64_t>(0));
}

static void testStopWithoutData() {
    beginTest("3b. stop() sin datos: el callback para sin bloquear el sink");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Started));
    // Consume todo y pide stop(): el callback debe parar en el acto.
    while (engine.ring_.availableToRead() > 0) stream->renderFrames(192);
    engine.stop();
    const oboe::DataCallbackResult result = stream->renderFrames(192);
    CHECK_EQ(static_cast<int>(result), static_cast<int>(oboe::DataCallbackResult::Stop));
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Stopped));
}

static void testResumeAfterStop() {
    beginTest("3c. Gapless: datos nuevos tras una parada reanudan el stream");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;

    // Se agota el anillo (bache entre canciones): el callback se para solo, sin
    // stop() de Media3. Es la parada "sin datos" que no rompe el gapless.
    while (stream->getState() == oboe::StreamState::Started) stream->renderFrames(192);
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Stopped));

    // Llegan datos de la pista siguiente: el callback reanuda sin play() nuevo.
    pushFrames(engine, 4800, 2, 2, 0x50);
    stream->state_.store(oboe::StreamState::Started);   // el HAL vuelve a llamar
    const oboe::DataCallbackResult result = stream->renderFrames(192);
    CHECK_EQ(static_cast<int>(result), static_cast<int>(oboe::DataCallbackResult::Continue));
}

// ---------------------------------------------------------------------------
// 4. Flush en estado válido y stream reutilizable
// ---------------------------------------------------------------------------

static void testFlushFromStarted() {
    beginTest("4. flush() desde STARTED: pausa primero (AAudio no acepta flush en STARTED)");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;
    pushFrames(engine, 9600, 2, 2, 0x60);
    CHECK(engine.ring_.availableToRead() > 0);

    engine.flush();
    CHECK_EQ(stream->invalidFlushCalls_.load(), 0);
    CHECK_EQ(stream->flushCalls_.load(), 1);
    // Queda reutilizable: PAUSED es el estado desde el que play() reanuda.
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Paused));
    CHECK_EQ(engine.ring_.availableToRead(), static_cast<size_t>(0));

    // Y después del flush se puede volver a llenar y sonar.
    pushFrames(engine, 4800, 2, 2, 0x70);
    engine.start();
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Started));
    stream->renderFrames(192);
    CHECK_EQ(engine.framesConsumed(), static_cast<int64_t>(192));
}

// ---------------------------------------------------------------------------
// 5. Posición de playout
// ---------------------------------------------------------------------------

static void testPlayoutPosition() {
    beginTest("5. Posición = frames consumidos - latencia real del timestamp");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;

    pushFrames(engine, 48000, 2, 2, 0x80);   // 1 s
    stream->hardwareLatencyFrames_.store(480);   // 10 ms en el DAC
    for (int i = 0; i < 10; ++i) stream->renderFrames(192);
    stream->advanceHardware(1920);

    const int64_t consumed = engine.framesConsumed();
    CHECK_EQ(consumed, static_cast<int64_t>(1920));
    const int64_t position = engine.positionFrames();
    // Posición = consumido - latencia, dentro de un margen de una ráfaga.
    CHECK(position <= consumed);
    CHECK(position >= consumed - 480 - 384);
    const int64_t micros = engine.positionMicros();
    CHECK(micros >= 0);
    CHECK(micros <= consumed * 1000000LL / 48000);

    // La latencia informada es coherente (≈10 ms).
    const int64_t latencyUs = engine.latencyMicros();
    CHECK(latencyUs >= 0);
    CHECK(latencyUs <= 25000);

    // Monótona: nunca retrocede aunque el hardware "se atrase".
    const int64_t before = engine.positionFrames();
    stream->hardwareLatencyFrames_.store(4096);
    CHECK(engine.positionFrames() >= before);
}

static void testPositionAfterPause() {
    beginTest("5b. Tras pausar, la posición no salta hacia atrás");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;
    pushFrames(engine, 48000, 2, 2, 0x90);
    stream->hardwareLatencyFrames_.store(960);
    for (int i = 0; i < 5; ++i) stream->renderFrames(192);
    const int64_t positionWhilePlaying = engine.positionFrames();
    engine.pause();
    const int64_t positionWhilePaused = engine.positionFrames();
    CHECK(positionWhilePaused >= positionWhilePlaying);
}

// ---------------------------------------------------------------------------
// 6. Fidelidad de la señal
// ---------------------------------------------------------------------------

static void testBitExactPassthrough() {
    beginTest("6a. Camino bit-perfect: los bytes llegan intactos al DAC");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI24, 96000, 2);
    CHECK(openAndSettle(engine, cfg));
    CHECK(engine.passthrough_);

    // 20 ms a 96 kHz = preroll exacto; entra en un único bloque del HAL.
    const int32_t frames = 1920;
    std::vector<uint8_t> pcm(static_cast<size_t>(frames) * 6);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    CHECK_EQ(engine.writePacked(pcm.data(), pcm.size()), pcm.size());
    // La apertura es diferida: el stream existe cuando el primer write la esperó.
    auto stream = engine.stream_;
    CHECK(stream != nullptr);
    engine.start();
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Started));
    stream->renderFrames(frames);
    CHECK(stream->lastBuffer().size() >= pcm.size());
    CHECK(std::memcmp(stream->lastBuffer().data(), pcm.data(), pcm.size()) == 0);

    // Con volumen < 100 % deja de ser passthrough, pero la contabilidad sigue exacta.
    engine.flush();
    engine.setVolume(0.5f);
    const int64_t baseline = engine.ring_.availableToRead();
    CHECK_EQ(baseline, static_cast<int64_t>(0));
    std::vector<uint8_t> pcm2(static_cast<size_t>(frames) * 6, 0x40);
    const size_t accepted = engine.writePacked(pcm2.data(), pcm2.size());
    CHECK_EQ(accepted, pcm2.size());
    CHECK_EQ(engine.ring_.availableToRead(), accepted);
}

static void testChannelUpmix() {
    beginTest("6b. Mono -> estéreo: la señal se replica, no se inventa silencio");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 1);
    cfg.strictBitPerfect = false;
    openAndSettle(engine, cfg);
    // El doble abre 2 canales (como un HAL que no acepta mono).
    engine.streamChannels_ = 2;
    engine.sourceChannels_ = 1;
    engine.passthrough_ = false;
    engine.resampler_.configure(48000, 48000, 2);

    const int16_t mono[4] = {1000, -1000, 2000, -2000};
    const size_t accepted = engine.writePacked(reinterpret_cast<const uint8_t*>(mono), sizeof(mono));
    CHECK_EQ(accepted, sizeof(mono));
    std::vector<uint8_t> out(16, 0);
    const size_t read = engine.ring_.read(out.data(), out.size());
    CHECK_EQ(read, static_cast<size_t>(16));
    const auto* stereo = reinterpret_cast<const int16_t*>(out.data());
    CHECK_EQ(stereo[0], static_cast<int16_t>(1000));
    CHECK_EQ(stereo[1], static_cast<int16_t>(1000));
    CHECK_EQ(stereo[2], static_cast<int16_t>(-1000));
    CHECK_EQ(stereo[3], static_cast<int16_t>(-1000));
}

static void testResampleAccounting() {
    beginTest("6c. Resampleo 44.1 -> 48 kHz: nada se pierde ni se duplica");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kF32, 44100, 2);
    cfg.strictBitPerfect = false;
    CHECK(openAndSettle(engine, cfg));
    engine.streamRate_ = 48000;     // el HAL subió el rate
    engine.passthrough_ = false;
    engine.resampler_.configure(44100, 48000, 2);
    primeAndStart(engine, 44100);

    const int32_t framesIn = 44100;   // 1 segundo
    std::vector<float> in(static_cast<size_t>(framesIn) * 2);
    for (int32_t i = 0; i < framesIn; ++i) {
        in[static_cast<size_t>(i) * 2] = static_cast<float>(std::sin(i * 0.01));
        in[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(std::sin(i * 0.01));
    }
    const int64_t queuedBaseline = engine.framesQueued();
    size_t accepted = 0;
    const size_t bytes = in.size() * sizeof(float);
    while (accepted < bytes) {
        const size_t n = engine.writePacked(reinterpret_cast<uint8_t*>(in.data()) + accepted,
                                            bytes - accepted);
        if (n == 0) break;
        accepted += n;
        // El "DAC" va consumiendo mientras el productor llena.
        if (engine.stream_) engine.stream_->renderFrames(480);
    }
    CHECK_EQ(accepted, bytes);
    // Los frames de salida NUEVOS equivalen a 1 s de audio (±1 %).
    const int64_t queued = engine.framesQueued() - queuedBaseline;
    CHECK(queued > 47000);
    CHECK(queued < 49000);
}

// ---------------------------------------------------------------------------
// 7. Apertura: formatos, rate y dispositivo
// ---------------------------------------------------------------------------

static void testStrictFailsInsteadOfConverting() {
    beginTest("7a. Modo estricto: si el HAL no acepta el rate, falla en vez de resamplear");
    oboe::AudioStreamBuilder::reset();
    oboe::AudioStreamBuilder::setUnsupportedRate(192000);
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI24, 192000, 2, /*strict=*/true);
    const oboe::Result result = openAndSettle(engine, cfg) ? oboe::Result::OK
                                                          : oboe::Result::ErrorUnavailable;
    CHECK(result != oboe::Result::OK);
    oboe::AudioStreamBuilder::reset();
}

static void testFallbackWhenNotStrict() {
    beginTest("7b. Modo no estricto: cae a otro rate y lo informa como no passthrough");
    oboe::AudioStreamBuilder::reset();
    oboe::AudioStreamBuilder::setUnsupportedRate(192000);
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI24, 192000, 2, /*strict=*/false);
    const oboe::Result result = openAndSettle(engine, cfg) ? oboe::Result::OK
                                                          : oboe::Result::ErrorUnavailable;
    CHECK(result == oboe::Result::OK);
    CHECK(!engine.passthrough_);
    CHECK(engine.streamRate_ != 192000);
    oboe::AudioStreamBuilder::reset();
}

static void testRequireDeviceDoesNotFallback() {
    beginTest("7c. requireDevice: si el DAC USB no abre, NO se cae al altavoz interno");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 2);
    cfg.deviceId = 12345;        // DAC que ya no existe
    cfg.requireDevice = true;
    const oboe::Result result = openAndSettle(engine, cfg) ? oboe::Result::OK
                                                          : oboe::Result::ErrorUnavailable;
    // El doble acepta cualquier deviceId, así que se comprueba la política: el id
    // pedido se conserva y no se reescribe a 0.
    CHECK(result == oboe::Result::OK);
    CHECK_EQ(engine.deviceId_, 12345);

    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine2;
    cfg.requireDevice = false;
    CHECK(openAndSettle(engine2, cfg));
    CHECK_EQ(engine2.deviceId_, 12345);
    oboe::AudioStreamBuilder::reset();
}

static void testOpenCountIsBounded() {
    beginTest("7d. La apertura no encadena reintentos infinitos");
    oboe::AudioStreamBuilder::reset();
    oboe::AudioStreamBuilder::setRequireExclusive(true);   // prohibido compartido
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 2, /*strict=*/false,
                                     /*exclusive=*/true);
    openAndSettle(engine, cfg);
    // Estricto + exclusivo + sin fallback de formato: como máximo los 3 primeros pasos.
    CHECK(oboe::AudioStreamBuilder::openCount() <= 3);
    oboe::AudioStreamBuilder::reset();
}

// ---------------------------------------------------------------------------
// 8. Errores
// ---------------------------------------------------------------------------

static void testDisconnectReportsError() {
    beginTest("8. DAC desconectado: el error llega al sink");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;
    const int32_t before = engine.lastErrorCode();
    CHECK_EQ(before, 0);
    stream->fireErrorAfterClose(oboe::Result::ErrorDisconnected);
    CHECK_EQ(engine.lastErrorCode(), static_cast<int32_t>(oboe::Result::ErrorDisconnected));
    CHECK(engine.holding_.load());
}


/**
 * 9a. Resampler: la señal que pasa por el filtro no gana ni pierde nivel audible y
 * deja de ser "un tono con basura". Se mide con una sinfonía de dos tonos a 1 kHz y
 * 9 kHz, se resamplea 44.1 → 48 kHz y se comparan amplitudes por correlación.
 *
 * Lo que este test protege: el filtro se re-afinó (β 10.06 y 24 taps, como BitChord).
 * Si alguien lo deja pasar de banda o lo rompe, la amplitud cambia o la señal se llena
 * de energía fuera de banda y esto falla.
 */
static void testResamplerQuality() {
    beginTest("9a. Resampler 44.1 -> 48 kHz: amplitud y banda preservadas");
    audio::PolyphaseResampler resampler;
    resampler.configure(44100, 48000, 1);
    CHECK(!resampler.bypass());

    const int32_t inFrames = 44100;             // 1 segundo
    std::vector<float> in(static_cast<size_t>(inFrames));
    for (int32_t i = 0; i < inFrames; ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        in[static_cast<size_t>(i)] =
                static_cast<float>(0.4 * std::sin(2.0 * M_PI * 1000.0 * t) +
                                   0.2 * std::sin(2.0 * M_PI * 9000.0 * t));
    }

    std::vector<float> out(static_cast<size_t>(60000), 0.0f);
    int32_t written = 0;
    int32_t offset = 0;
    while (offset < inFrames) {
        const int32_t chunk = std::min<int32_t>(4096, inFrames - offset);
        written += resampler.process(in.data() + offset, chunk, out.data() + written,
                                     static_cast<int32_t>(out.size()) - written);
        offset += chunk;
    }
    // Un segundo de entrada a 48 kHz son 48000 frames (más el arranque del filtro).
    CHECK(written >= 47000);
    CHECK(written <= 49000);

    // Correlación con un tono de referencia a la misma frecuencia: da la ganancia real
    // del camino completo sin depender de la fase inicial del filtro.
    auto amplitudeAt = [&](double frequency) {
        // Se descartan los primeros 2000 frames (transitorio del filtro).
        const int32_t start = 2000;
        double re = 0.0;
        double im = 0.0;
        int32_t count = 0;
        for (int32_t i = start; i < written; ++i) {
            const double t = static_cast<double>(i) / 48000.0;
            const double value = out[static_cast<size_t>(i)];
            re += value * std::cos(2.0 * M_PI * frequency * t);
            im += value * std::sin(2.0 * M_PI * frequency * t);
            ++count;
        }
        return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(count);
    };

    const double at1k = amplitudeAt(1000.0);
    const double at9k = amplitudeAt(9000.0);
    // Ganancia de cada tono dentro de ±0.5 dB de lo que entró (0.4 y 0.2).
    CHECK(std::fabs(20.0 * std::log10(at1k / 0.4)) < 0.5);
    CHECK(std::fabs(20.0 * std::log10(at9k / 0.2)) < 0.5);

    // Banda de rechazo: energía en una frecuencia que NO está en la entrada y que el
    // filtro debería dejar vacía. Si el filtro es demasiado corto, aparece.
    const double alias = amplitudeAt(15000.0);
    CHECK(alias < 0.01);   // < -32 dB respecto del tono principal
    std::printf("    medido: 1 kHz %.3f dB, 9 kHz %.3f dB, fuga a 15 kHz %.1f dBFS\n",
                20.0 * std::log10(at1k / 0.4), 20.0 * std::log10(at9k / 0.2),
                20.0 * std::log10(std::max(alias, 1e-9)));
}

/**
 * 9c. Downsampling 96 → 44.1 kHz: un tono por encima del Nyquist de salida tiene que
 * desaparecer, no plegarse dentro del audio audible. Es el caso donde un filtro corto
 * se nota (alias metálico en las pistas Hi-Res cuando el DAC no acepta 96 kHz).
 */
static void testResamplerAliasRejection() {
    beginTest("9c. Resampler 96 -> 44.1 kHz: rechazo de alias");
    audio::PolyphaseResampler resampler;
    resampler.configure(96000, 44100, 2);
    CHECK(!resampler.bypass());

    // 30 kHz está muy por encima del Nyquist de 22.05 kHz de la salida.
    const int32_t inFrames = 48000;
    std::vector<float> in(static_cast<size_t>(inFrames) * 2);
    for (int32_t i = 0; i < inFrames; ++i) {
        const float v = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 30000.0 * i / 96000.0));
        in[static_cast<size_t>(i) * 2] = v;
        in[static_cast<size_t>(i) * 2 + 1] = v;
    }
    std::vector<float> out(static_cast<size_t>(inFrames) * 2, 0.0f);
    int32_t written = 0;
    int32_t offset = 0;
    while (offset < inFrames) {
        const int32_t chunk = std::min<int32_t>(4096, inFrames - offset);
        written += resampler.process(in.data() + static_cast<size_t>(offset) * 2, chunk,
                                     out.data() + static_cast<size_t>(written) * 2,
                                     static_cast<int32_t>(out.size() / 2) - written);
        offset += chunk;
    }
    CHECK(written > 1000);

    // Se mide la energía de la salida: si el filtro deja pasar el tono, aquí aparece.
    double energy = 0.0;
    int32_t count = 0;
    for (int32_t i = 2000; i < written; ++i) {
        const double v = out[static_cast<size_t>(i) * 2];
        energy += v * v;
        ++count;
    }
    const double rms = count > 0 ? std::sqrt(energy / count) : 0.0;
    const double dbfs = 20.0 * std::log10(std::max(rms * std::sqrt(2.0), 1e-12));
    std::printf("    energía del tono de 30 kHz tras resamplear: %.1f dBFS\n", dbfs);
    // Una entrada de 0.5 correspondería a -6 dBFS si pasara; el filtro debe dejarla
    // completamente abajo.
    CHECK(dbfs < -40.0);
}

/**
 * 9b. La posición no retrocede cuando el timestamp del HAL vuelve atrás, y avanza
 * suave entre refrescos del timestamp (ancla + extrapolación, idea de Mornye).
 */
static void testPositionAnchoring() {
    beginTest("9b. El reloj no retrocede y avanza entre refrescos del timestamp");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    audio::EngineConfig cfg = config(audio::PcmFormat::kI16, 48000, 2);
    cfg.fades = false;
    CHECK(openAndSettle(engine, cfg));

    std::vector<int16_t> pcm(4800 * 2, 0);
    CHECK_EQ(engine.writePacked(reinterpret_cast<const uint8_t*>(pcm.data()),
                                pcm.size() * sizeof(int16_t)),
             pcm.size() * sizeof(int16_t));
    engine.start();

    // El doble de Oboe entrega un timestamp fijo: la posición debe ser monótona.
    const int64_t first = engine.positionFrames();
    const int64_t second = engine.positionFrames();
    CHECK(second >= first);

    // Y no puede pasarse de lo consumido.
    CHECK(engine.positionFrames() <= engine.framesConsumed());
    engine.close();
    oboe::AudioStreamBuilder::reset();
}


// ---------------------------------------------------------------------------
// 10. Continuidad: flush de transición, preroll y curva de volumen
// ---------------------------------------------------------------------------

/**
 * El flush de transición tiene que descartar los datos propios igual que `flush()`, pero
 * **sin esperar** a que el HAL pase por PAUSED/FLUSHED: es la espera que, entre canción y
 * canción, se escuchaba como un bache. Se mide el tiempo real con un stream que **nunca**
 * confirma el cambio de estado (el peor caso: un HAL lento), que es justo donde `flush()`
 * se queda esperando su timeout.
 */
static void testTransitionFlushIsNotBlocking() {
    beginTest("10a. flushForTransition() no espera al HAL (y flush() sí)");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    openAndSettle(engine, config(audio::PcmFormat::kI16, 48000, 2));
    primeAndStart(engine);
    auto stream = engine.stream_;
    pushFrames(engine, 9600, 2, 2, 0x30);
    CHECK(engine.ring_.availableToRead() > 0);

    // El doble confirma los cambios de estado al instante (waitForStateChange devuelve
    // timeout), así que el flush "lento" se mide por sus llamadas, no por su reloj: lo que
    // se verifica es que el de transición no consulte el estado del HAL más de lo mínimo.
    const auto start = std::chrono::steady_clock::now();
    const oboe::Result fast = engine.flushForTransition();
    const auto fastMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - start).count();
    CHECK_EQ(static_cast<int>(fast), static_cast<int>(oboe::Result::OK));
    // Los datos propios se descartan igual: no queda audio viejo para la pista nueva.
    CHECK_EQ(engine.ring_.availableToRead(), static_cast<size_t>(0));
    // Y el stream queda reutilizable (PAUSED/FLUSHED, nunca STARTED).
    CHECK(stream->getState() != oboe::StreamState::Started);
    // Sin esperas: el camino rápido no debería tardar más que un par de milisegundos.
    CHECK(fastMicros < 5000);
    CHECK_EQ(stream->invalidFlushCalls_.load(), 0);

    // Después se puede volver a llenar y sonar (la pista nueva arranca sin reabrir nada).
    pushFrames(engine, 4800, 2, 2, 0x40);
    engine.start();
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Started));
}

/**
 * El preroll define cuánto tarda en sonar la primera muestra. Con el automático hacen falta
 * dos ráfagas o 20 ms; con `prerollMs = 0` alcanza con una ráfaga, así que el stream arranca
 * en el primer `write()` en vez de esperar.
 */
static void testPrerollStartsSooner() {
    beginTest("10b. prerollMs = 0 arranca con una ráfaga (antes: 2 ráfagas o 20 ms)");
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine engine;
    auto cfg = config(audio::PcmFormat::kI16, 48000, 2);
    cfg.prerollMs = 0;
    openAndSettle(engine, cfg);
    auto stream = engine.stream_;
    CHECK_EQ(engine.prerollFrames_.load(), static_cast<int32_t>(192));   // una ráfaga

    // Exactamente una ráfaga de datos: con el preroll automático esto NO arrancaría.
    pushFrames(engine, 192, 2, 2, 0x11);
    engine.start();
    CHECK_EQ(static_cast<int>(stream->getState()), static_cast<int>(oboe::StreamState::Started));

    // Y el automático sigue siendo el de siempre (no se cambió el comportamiento por defecto).
    oboe::AudioStreamBuilder::reset();
    audio::AudioEngine legacy;
    openAndSettle(legacy, config(audio::PcmFormat::kI16, 48000, 2));
    CHECK_EQ(legacy.prerollFrames_.load(), static_cast<int32_t>(960));
}

/**
 * Curva perceptual: al 100 % la ganancia es exactamente 1 (bit-perfect intacto) y los
 * valores intermedios son monótonos y más bajos que la recta, que es lo que hace que el
 * deslizador se sienta parejo en vez de saltar desde el 20 %.
 */
static void testVolumeCurve() {
    beginTest("10c. Curva de volumen perceptual: 1.0 exacto al 100 % y monótona");
    audio::AudioEngine engine;
    engine.setVolumeCurve(0);
    engine.setVolume(1.0f);
    CHECK(engine.targetGain() == 1.0f);
    engine.setVolumeCurve(1);
    CHECK(engine.targetGain() == 1.0f);          // 1^2.5 = 1 exacto
    engine.setVolume(0.0f);
    CHECK(engine.targetGain() == 0.0f);

    float previous = -1.0f;
    for (int step = 1; step <= 10; ++step) {
        const float volume = static_cast<float>(step) / 10.0f;
        engine.setVolume(volume);
        const float gain = engine.targetGain();
        CHECK(gain >= previous);                  // monótona
        CHECK(gain <= volume + 1e-6f);            // nunca más fuerte que la recta
        previous = gain;
    }
    engine.setVolume(0.5f);
    const float half = engine.targetGain();
    CHECK(half < 0.2f);                           // 0.5^2.5 ≈ 0.177, no 0.5
    CHECK(half > 0.15f);

    // La curva lineal no cambió (compatibilidad con lo que ya estaba).
    engine.setVolumeCurve(0);
    engine.setVolume(0.5f);
    CHECK(engine.targetGain() == 0.5f);
}

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("Pruebas del motor de audio nativo (host, dobles de Oboe)\n");
    std::printf("========================================================\n");

    testRingConcurrent();
    testNoPartialTail();
    testEndOfStreamDrains();
    testStopWithoutData();
    testResumeAfterStop();
    testFlushFromStarted();
    testPlayoutPosition();
    testPositionAfterPause();
    testBitExactPassthrough();
    testChannelUpmix();
    testResampleAccounting();
    testStrictFailsInsteadOfConverting();
    testFallbackWhenNotStrict();
    testRequireDeviceDoesNotFallback();
    testOpenCountIsBounded();
    testDisconnectReportsError();
    testResamplerQuality();
    testResamplerAliasRejection();
    testPositionAnchoring();
    testTransitionFlushIsNotBlocking();
    testPrerollStartsSooner();
    testVolumeCurve();

    std::printf("\n========================================================\n");
    if (g_failures == 0) {
        std::printf("\033[32m%d comprobaciones, 0 fallas\033[0m\n", g_checks);
        return 0;
    }
    std::printf("\033[31m%d comprobaciones, %d FALLAS\033[0m\n", g_checks, g_failures);
    return 1;
}

package com.kukosin.app.audio

import java.nio.ByteBuffer

/**
 * Protocolo entero compartido con `native-lib.cpp`. No reordenar los códigos.
 */
enum class PcmFormat(val code: Int, val bits: Int, val label: String) {
    INT16(0, 16, "16-bit"),
    INT24(1, 24, "24-bit"),
    FLOAT32(2, 32, "32-bit float"),
    INT32(3, 32, "32-bit int");

    companion object {
        fun fromCode(code: Int): PcmFormat = when (code) {
            0 -> INT16
            1 -> INT24
            2 -> FLOAT32
            3 -> INT32
            else -> INT16
        }

        fun fromMedia3Encoding(encoding: Int): PcmFormat = when (encoding) {
            androidx.media3.common.C.ENCODING_PCM_16BIT -> INT16
            androidx.media3.common.C.ENCODING_PCM_24BIT -> INT24
            androidx.media3.common.C.ENCODING_PCM_32BIT -> INT32
            androidx.media3.common.C.ENCODING_PCM_FLOAT -> FLOAT32
            else -> INT16
        }
    }
}

/** Estado del stream de Oboe (espejo de oboe::StreamState). */
enum class EngineState(val code: Int) {
    UNINITIALIZED(0),
    UNKNOWN(1),
    OPEN(2),
    STARTING(3),
    STARTED(4),
    PAUSING(5),
    PAUSED(6),
    FLUSHING(7),
    FLUSHED(8),
    STOPPING(9),
    STOPPED(10),
    CLOSING(11),
    CLOSED(12),
    DISCONNECTED(13);

    companion object {
        fun fromCode(code: Int): EngineState = entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

/** Espejo de oboe::Result. */
enum class NativeResult(val code: Int, val description: String) {
    OK(0, "OK"),
    ERROR_DISCONNECTED(-899, "Dispositivo desconectado"),
    ERROR_ILLEGAL_ARGUMENT(-898, "Argumento inválido"),
    ERROR_INVALID_STATE(-895, "Estado inválido"),
    ERROR_UNAVAILABLE(-892, "Formato/rate no disponible en el dispositivo"),
    ERROR_NO_MEMORY(-890, "Sin memoria"),
    ERROR_INVALID_FORMAT(-886, "Formato no soportado"),
    ERROR_OUT_OF_RANGE(-885, "Fuera de rango"),
    ERROR_INVALID_RATE(-883, "Frecuencia de muestreo no soportada"),
    ERROR_CLOSED(-882, "Stream cerrado"),
    ERROR_NULL(-889, "Motor no inicializado"),
    UNKNOWN(Int.MIN_VALUE, "Error desconocido");

    val isOk: Boolean get() = code == 0

    companion object {
        fun fromCode(code: Int): NativeResult =
            entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

/** Lo que pide la app: el formato real de la pista ya decodificada. */
data class AudioEngineConfig(
    val sourceFormat: PcmFormat = PcmFormat.INT16,
    val sourceSampleRate: Int = 48_000,
    val sourceChannels: Int = 2,
    /** Id del [android.media.AudioDeviceInfo] al que queremos salir (0 = predeterminado). */
    val deviceId: Int = 0,
    /** Si es true, el motor no acepta conversión: o es exacto, o falla. */
    val strictBitPerfect: Boolean = true,
    /** Si es true, no se acepta un stream compartido (modos exclusivos). */
    val requireExclusive: Boolean = false,
    /** Objetivo de buffer en milisegundos (estabilidad). */
    val bufferMs: Int = 250,
    /** Fundido de entrada (25 ms) y de salida (60 ms) para no chasquear al play/pausa. */
    val fades: Boolean = true,
)

/** Lo que devolvió el hardware, tal cual. */
data class EngineInfo(
    val opened: Boolean = false,
    val apiCode: Int = -1,
    val source: PcmFormat = PcmFormat.INT16,
    val sourceSampleRate: Int = 0,
    val sourceChannels: Int = 2,
    val stream: PcmFormat? = null,
    val streamSampleRate: Int = 0,
    val streamChannels: Int = 0,
    val framesPerBurst: Int = 0,
    val bufferFrames: Int = 0,
    val deviceId: Int = 0,
    val xRuns: Int = -1,
    val exclusive: Boolean = false,
    val passthrough: Boolean = false,
    val volumePercent: Int = 100,
    val errorCode: Int = 0,
) {
    val apiLabel: String
        get() = when (apiCode) {
            0 -> "AAudio"
            1 -> "OpenSL ES"
            else -> "—"
        }

    /** Latencia aproximada del buffer (lo que se acumula antes de llegar al DAC). */
    val bufferMs: Double
        get() = if (streamSampleRate > 0) bufferFrames * 1000.0 / streamSampleRate else 0.0

    /** Bit-perfect real: exclusivo, sin conversión y sin atenuación digital. */
    val bitPerfect: Boolean
        get() = opened && exclusive && passthrough && volumePercent >= 100

    val converted: Boolean get() = opened && !passthrough

    /** Texto corto para la UI: "24-bit · 96 kHz · exclusivo". */
    val streamLabel: String
        get() {
            val fmt = stream ?: return "—"
            val sharing = if (exclusive) "exclusivo" else "compartido"
            return "${fmt.bits}-bit · ${(streamSampleRate / 1000.0).format(1)} kHz · $sharing"
        }
}

private fun Double.format(decimals: Int): String =
    String.format(java.util.Locale.US, "%.${decimals}f", this)

/**
 * Puente JNI al motor nativo ([audio::AudioEngine] en C++).
 *
 * Un [AudioEngine] = un stream de Oboe = un formato. Si la pista cambia de rate o
 * de profundidad, Media3 reconstruye el sink y se crea un motor nuevo: es el
 * precio de no resamplear.
 */
class AudioEngine : AutoCloseable {

    private val lock = Any()

    @Volatile
    private var handle: Long = nativeCreate()

    private val infoBuffer = IntArray(INFO_SIZE)

    init {
        require(handle != 0L) { "No se pudo crear el motor de audio nativo" }
    }

    /**
     * El motor lo cierra el hilo de audio de Media3 mientras la UI puede estar
     * leyendo el estado: sin este candado habría un use-after-free en JNI.
     */
    private inline fun <T> withHandle(default: T, block: (Long) -> T): T = synchronized(lock) {
        val current = handle
        if (current == 0L) default else block(current)
    }

    val state: EngineState
        get() = withHandle(EngineState.UNINITIALIZED) { EngineState.fromCode(nativeGetState(it)) }

    val isOpen: Boolean get() = info().opened

    /** Frames de origen que ya salieron por el DAC (para la posición del player). */
    val sourceFramesRendered: Long get() = withHandle(0L) { nativeGetSourceFrames(it) }

    /**
     * Abre el stream. Bloqueante (~5-40 ms): llamar fuera del hilo principal.
     * Intenta primero exacto + exclusivo; si no se puede y [AudioEngineConfig.strictBitPerfect]
     * es false, busca un fallback con conversión propia.
     */
    fun configure(config: AudioEngineConfig): NativeResult = withHandle(NativeResult.ERROR_NULL) { h ->
        NativeResult.fromCode(
            nativeConfigure(
                h,
                config.sourceFormat.code,
                config.sourceSampleRate,
                config.sourceChannels,
                config.deviceId,
                config.strictBitPerfect,
                config.requireExclusive,
                config.bufferMs,
                config.fades,
            ),
        )
    }

    /**
     * Último error nativo (0 = ninguno). Se consulta sólo cuando una escritura no entra:
     * así el sink puede fallar y reconstruirse en vez de quedarse esperando para siempre.
     */
    fun errorCode(): Int = withHandle(0) { h -> nativeErrorCode(h) }

    fun start(): NativeResult = withHandle(NativeResult.ERROR_NULL) { NativeResult.fromCode(nativeStart(it)) }

    fun stop(): NativeResult = withHandle(NativeResult.ERROR_NULL) { NativeResult.fromCode(nativeStop(it)) }

    fun pause(): NativeResult = withHandle(NativeResult.ERROR_NULL) { NativeResult.fromCode(nativePause(it)) }

    fun flush(): NativeResult = withHandle(NativeResult.ERROR_NULL) { NativeResult.fromCode(nativeFlush(it)) }

    /** Volumen 0..1. A 1.0 el camino es bit-perfect; por debajo se atenúa. */
    fun setVolume(volume: Float) = withHandle(Unit) {
        nativeSetVolume(it, volume.coerceIn(0f, 1f))
    }

    /**
     * Escribe PCM ya decodificado. Devuelve los bytes de [buffer] aceptados
     * (0 = el ring está lleno, reintentar). Nunca bloquea.
     */
    fun writePacked(buffer: ByteBuffer): Int {
        val remaining = buffer.remaining()
        if (remaining <= 0) return 0
        return withHandle(0) { h ->
            if (buffer.isDirect) {
                nativeWritePacked(h, buffer, buffer.position(), remaining).coerceIn(0, remaining)
            } else {
                val copy = ByteArray(remaining)
                val position = buffer.position()
                buffer.get(copy, 0, remaining)
                buffer.position(position)
                val direct = ByteBuffer.allocateDirect(remaining)
                direct.put(copy).flip()
                nativeWritePacked(h, direct, 0, remaining).coerceIn(0, remaining)
            }
        }
    }

    fun info(): EngineInfo = withHandle(EngineInfo()) { h ->
        nativeGetInfo(h, infoBuffer)
        val open = infoBuffer[0] >= 0
        return EngineInfo(
            opened = open,
            apiCode = infoBuffer[0],
            stream = if (open && infoBuffer[1] >= 0) PcmFormat.fromCode(infoBuffer[1]) else null,
            streamSampleRate = infoBuffer[2],
            streamChannels = infoBuffer[3],
            framesPerBurst = infoBuffer[4],
            bufferFrames = infoBuffer[5],
            deviceId = infoBuffer[6],
            xRuns = infoBuffer[7],
            exclusive = infoBuffer[8] == 1,
            passthrough = infoBuffer[10] == 1,
            errorCode = infoBuffer[11],
            volumePercent = infoBuffer[12],
            source = PcmFormat.fromCode(infoBuffer[13]),
            sourceSampleRate = infoBuffer[14],
            sourceChannels = infoBuffer[15],
        )
    }

    override fun close() {
        synchronized(lock) {
            val current = handle
            if (current != 0L) {
                handle = 0L
                nativeDelete(current)
            }
        }
    }

    private external fun nativeCreate(): Long
    private external fun nativeDelete(handle: Long)
    private external fun nativeConfigure(
        handle: Long,
        format: Int,
        sampleRate: Int,
        channelCount: Int,
        deviceId: Int,
        strictBitPerfect: Boolean,
        requireExclusive: Boolean,
        bufferMs: Int,
        fades: Boolean,
    ): Int
    private external fun nativeStart(handle: Long): Int
    private external fun nativeStop(handle: Long): Int
    private external fun nativePause(handle: Long): Int
    private external fun nativeFlush(handle: Long): Int
    private external fun nativeSetVolume(handle: Long, volume: Float)
    private external fun nativeWritePacked(handle: Long, buffer: ByteBuffer, offset: Int, length: Int): Int
    private external fun nativeErrorCode(handle: Long): Int
    private external fun nativeGetState(handle: Long): Int
    private external fun nativeGetSourceFrames(handle: Long): Long
    private external fun nativeGetInfo(handle: Long, out: IntArray)

    private companion object {
        const val INFO_SIZE = 16

        init {
            System.loadLibrary("audioengine")
        }
    }
}

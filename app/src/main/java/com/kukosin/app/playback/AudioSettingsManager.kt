package com.kukosin.app.playback

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Ajustes de audio. Ya no hay "driver", ni rate objetivo, ni formato PCM: el motor
 * siempre abre el DAC con el formato exacto del archivo. Lo único configurable es la
 * política (estricto o con fallback) y el tamaño de buffer.
 */
data class AudioConfig(
    /** Camino de salida: mezclador, exclusivo integrado, directo o DAC USB. */
    val outputMode: OutputMode = OutputMode.AAUDIO_EXCLUSIVE,
    /**
     * Si está activo, el motor NUNCA convierte: o abre exacto, o falla y avisa.
     * Apagado (recomendado), intenta bit-perfect primero y sólo si el DAC no lo
     * acepta pasa a conversión propia.
     */
    val strictBitPerfect: Boolean = false,
    /** Buffer objetivo en milisegundos: más grande = más estable, más latencia. */
    val bufferMs: Int = 250,
    /**
     * Fundidos de 25 ms al arrancar y 60 ms al pausar/detener, más un fundido corto
     * después de un salto. Evita los clics del motor. Apagado = señal intacta desde
     * el primer frame (purista).
     */
    val fades: Boolean = true,
    val autoAnalyze: Boolean = false,
    val decoder: String = "FFmpeg (Lossless)",
    val queueCompactMode: Boolean = false,
    val showQueueBadge: Boolean = true,
    val queueShowHistory: Boolean = true,
) {
    /**
     * Sólo lo que obliga a reconstruir el ExoPlayer. Cambiar el aspecto de la cola
     * no debe cortar la reproducción.
     */
    val audioSignature: String
        get() = "$outputMode|$strictBitPerfect|$bufferMs|$fades|$decoder"

    companion object {
        val BUFFER_OPTIONS = listOf(100, 250, 500, 1000)
    }
}

object AudioSettingsManager {
    private const val PREFS_NAME = "kukosin_audio_settings_v2"

    private var prefs: SharedPreferences? = null
    private val state = MutableStateFlow(AudioConfig())
    val configState: StateFlow<AudioConfig> = state.asStateFlow()

    fun init(context: Context) {
        if (prefs == null) {
            prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            load()
        }
    }

    private fun load() {
        val p = prefs ?: return
        state.value = AudioConfig(
            outputMode = OutputMode.fromName(p.getString("output_mode", null)),
            strictBitPerfect = p.getBoolean("strict_bit_perfect", false),
            bufferMs = p.getInt("buffer_ms", 250).coerceIn(40, 2000),
            fades = p.getBoolean("fades", true),
            autoAnalyze = p.getBoolean("auto_analyze", false),
            decoder = p.getString("decoder", "FFmpeg (Lossless)") ?: "FFmpeg (Lossless)",
            queueCompactMode = p.getBoolean("queue_compact_mode", false),
            showQueueBadge = p.getBoolean("show_queue_badge", true),
            queueShowHistory = p.getBoolean("queue_show_history", true),
        )
    }

    fun setOutputMode(mode: OutputMode) {
        prefs?.edit()?.putString("output_mode", mode.name)?.apply()
        state.value = state.value.copy(outputMode = mode)
    }

    fun setStrictBitPerfect(enabled: Boolean) {
        prefs?.edit()?.putBoolean("strict_bit_perfect", enabled)?.apply()
        state.value = state.value.copy(strictBitPerfect = enabled)
    }

    fun setBufferMs(ms: Int) {
        val value = ms.coerceIn(40, 2000)
        prefs?.edit()?.putInt("buffer_ms", value)?.apply()
        state.value = state.value.copy(bufferMs = value)
    }

    fun setFades(enabled: Boolean) {
        prefs?.edit()?.putBoolean("fades", enabled)?.apply()
        state.value = state.value.copy(fades = enabled)
    }

    fun setAutoAnalyze(enabled: Boolean) {
        prefs?.edit()?.putBoolean("auto_analyze", enabled)?.apply()
        state.value = state.value.copy(autoAnalyze = enabled)
    }

    fun setDecoder(decoder: String) {
        prefs?.edit()?.putString("decoder", decoder)?.apply()
        state.value = state.value.copy(decoder = decoder)
    }

    fun setQueueCompactMode(compact: Boolean) {
        prefs?.edit()?.putBoolean("queue_compact_mode", compact)?.apply()
        state.value = state.value.copy(queueCompactMode = compact)
    }

    fun setShowQueueBadge(show: Boolean) {
        prefs?.edit()?.putBoolean("show_queue_badge", show)?.apply()
        state.value = state.value.copy(showQueueBadge = show)
    }

    fun setQueueShowHistory(show: Boolean) {
        prefs?.edit()?.putBoolean("queue_show_history", show)?.apply()
        state.value = state.value.copy(queueShowHistory = show)
    }
}

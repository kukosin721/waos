package com.kukosin.app.audio

import android.media.AudioFormat
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioOutput
import androidx.media3.exoplayer.audio.AudioOutputProvider
import com.kukosin.app.playback.AudioRoute
import com.kukosin.app.playback.AudioSettingsManager
import com.kukosin.app.playback.BitPerfectMixer
import com.kukosin.app.playback.OutputMode
import java.util.concurrent.CopyOnWriteArraySet

/**
 * Salida de audio servida por el motor de Oboe, según el [OutputMode] elegido.
 *
 * `DISABLED` no pasa por acá: en ese modo el sink usa AudioTrack (mezclador de Android).
 *
 * Para los tres modos exclusivos el motor intenta, en orden:
 * 1. el formato exacto de la pista, exclusivo, sin fast-mixer  → bit-perfect;
 * 2. el formato exacto, compartido (sólo si el modo lo permite);
 * 3. otro rate/formato con conversión polifásica propia (sólo si el modo no es estricto).
 */
@UnstableApi
class OboeAudioOutputProvider(
    private val mode: OutputMode,
) : AudioOutputProvider {

    private val listeners = CopyOnWriteArraySet<AudioOutputProvider.Listener>()

    override fun getFormatSupport(
        formatConfig: AudioOutputProvider.FormatConfig,
    ): AudioOutputProvider.FormatSupport {
        val encoding = formatConfig.format.pcmEncoding
        val supported = encoding == C.ENCODING_PCM_16BIT ||
            encoding == C.ENCODING_PCM_24BIT ||
            encoding == C.ENCODING_PCM_32BIT ||
            encoding == C.ENCODING_PCM_FLOAT
        return if (supported) {
            AudioOutputProvider.FormatSupport.Builder()
                .setFormatSupportLevel(AudioOutputProvider.FORMAT_SUPPORTED_DIRECTLY)
                .build()
        } else {
            AudioOutputProvider.FormatSupport.UNSUPPORTED
        }
    }

    override fun getOutputConfig(
        formatConfig: AudioOutputProvider.FormatConfig,
    ): AudioOutputProvider.OutputConfig {
        val format = formatConfig.format
        val channels = format.channelCount.coerceAtLeast(1)
        val channelMask = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
        val encoding = when (format.pcmEncoding) {
            C.ENCODING_PCM_16BIT, C.ENCODING_PCM_24BIT, C.ENCODING_PCM_32BIT, C.ENCODING_PCM_FLOAT ->
                format.pcmEncoding
            else -> C.ENCODING_PCM_16BIT
        }
        // La frecuencia es SIEMPRE la del archivo: así no hay resample en el camino.
        val sampleRate = if (format.sampleRate > 0) format.sampleRate else 48_000
        return AudioOutputProvider.OutputConfig.Builder()
            .setEncoding(encoding)
            .setSampleRate(sampleRate)
            .setChannelMask(channelMask)
            .build()
    }

    override fun getAudioOutput(config: AudioOutputProvider.OutputConfig): AudioOutput {
        val settings = AudioSettingsManager.configState.value
        val target = AudioRoute.deviceFor(mode)
        if (mode.requiresUsb && target == null) {
            AudioEngineStatus.error(
                "No hay DAC USB conectado: saliendo por el dispositivo predeterminado. " +
                    "Conectá el DAC y volvé a intentar.",
            )
        }

        val channels = Integer.bitCount(config.channelMask).coerceAtLeast(1)
        val sourceFormat = PcmFormat.fromMedia3Encoding(config.encoding)

        fun open(deviceId: Int): AudioEngine {
            val engine = AudioEngine()
            val result = engine.configure(
                AudioEngineConfig(
                    sourceFormat = sourceFormat,
                    sourceSampleRate = config.sampleRate,
                    sourceChannels = channels,
                    deviceId = deviceId,
                    strictBitPerfect = settings.strictBitPerfect,
                    requireExclusive = mode.requiresExclusive,
                    bufferMs = settings.bufferMs,
                    fades = settings.fades,
                ),
            )
            if (!result.isOk) {
                engine.close()
                throw AudioOutputProvider.InitializationException(
                    IllegalStateException(
                        "Oboe no pudo abrir ${sourceFormat.bits}-bit @ ${config.sampleRate} Hz: " +
                            result.description,
                    ),
                )
            }
            return engine
        }

        val engine = try {
            open(target?.id ?: 0)
        } catch (first: AudioOutputProvider.InitializationException) {
            // El id puede quedar viejo si el DAC se reconectó: reintento por defecto.
            if (target == null) {
                AudioEngineStatus.error(
                    "No se pudo abrir la salida (${sourceFormat.bits}-bit @ ${config.sampleRate} Hz). " +
                        "Probá " +
                        if (mode.requiresExclusive) "otro modo de salida o desactivá el modo estricto." else
                            "otro modo de salida.",
                )
                throw first
            }
            try {
                open(0)
            } catch (second: AudioOutputProvider.InitializationException) {
                AudioEngineStatus.error(
                    "No se pudo abrir ${deviceLabel()} (${sourceFormat.bits}-bit @ " +
                        "${config.sampleRate} Hz): ${second.message}. " +
                        "Este modo necesita un stream exclusivo que el dispositivo no está dando.",
                )
                throw second
            }
        }

        // Hint al mezclador de Android 14+ por si el stream terminó compartido.
        BitPerfectMixer.apply(
            AudioRoute.current.value,
            config.sampleRate,
            config.encoding,
            config.channelMask,
        )

        val builtinSpeaker = target?.type == android.media.AudioDeviceInfo.TYPE_BUILTIN_SPEAKER ||
            AudioRoute.current.value.isBuiltin
        AudioEngineStatus.attach(engine, deviceLabel(), mode, builtinSpeaker)
        return OboeAudioOutput(engine, config.sampleRate, mode, AudioRoute.appContext)
    }

    private fun deviceLabel(): String {
        val target = AudioRoute.deviceFor(mode) ?: return when {
            mode.targetsUsb -> "Sin DAC USB"
            else -> "Salida integrada"
        }
        val product = target.productName?.toString()?.trim()
        return if (!product.isNullOrEmpty()) product else when {
            mode.targetsUsb -> "DAC USB"
            else -> "DAC integrado"
        }
    }

    override fun addListener(listener: AudioOutputProvider.Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: AudioOutputProvider.Listener) {
        listeners.remove(listener)
    }

    override fun release() {
        listeners.clear()
        AudioEngineStatus.detach()
    }
}

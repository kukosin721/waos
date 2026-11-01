package com.kukosin.app.playback

import android.content.Context
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory.EXTENSION_RENDERER_MODE_OFF
import androidx.media3.exoplayer.DefaultRenderersFactory.EXTENSION_RENDERER_MODE_ON
import androidx.media3.exoplayer.DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import com.kukosin.app.audio.OboeAudioOutputProvider
import io.github.anilbeesetti.nextlib.media3ext.ffdecoder.NextRenderersFactory

/**
 * Cadena de audio: decoder → (futuro: EqAudioProcessor) →
 * [OboeAudioOutputProvider] → Oboe → DAC.
 *
 * No hay ramas por "driver": la única salida es Oboe, y el sink nunca convierte a
 * float (eso requantizaría y nos sacaría del bit-perfect).
 */
@UnstableApi
class HiFiRenderersFactory(context: Context) : NextRenderersFactory(context) {

    init {
        val config = AudioSettingsManager.configState.value
        val extensionMode = when (config.decoder) {
            "MediaCodec" -> EXTENSION_RENDERER_MODE_OFF
            "Automático" -> EXTENSION_RENDERER_MODE_ON
            else -> EXTENSION_RENDERER_MODE_PREFER
        }
        setExtensionRendererMode(extensionMode)
        // El PCM va al DAC con la profundidad con la que salió del decoder.
        setEnableAudioFloatOutput(false)
        setEnableAudioTrackPlaybackParams(false)
    }

    override fun buildAudioSink(
        context: Context,
        enableFloatOutput: Boolean,
        enableAudioTrackPlaybackParams: Boolean,
    ): AudioSink {
        val processors = ArrayList<AudioProcessor>()
        // TODO(ronda EQ): acá entra el EqAudioProcessor cuando se porte el motor del
        // ecualizador a esta rama. Hoy la cadena va sin DSP: el motor no procesa nada
        // y el bit-perfect es memcpy puro.

        val mode = AudioSettingsManager.configState.value.outputMode
        val builder = DefaultAudioSink.Builder(context)
            .setEnableFloatOutput(false)
            .setAudioProcessors(processors.toTypedArray())

        // Modo "Desactivado": AudioTrack puro, el mezclador de Android decide.
        // Los otros tres abren el motor de Oboe con su propia política.
        if (mode.usesEngine) {
            builder.setAudioOutputProvider(OboeAudioOutputProvider(mode))
        }
        return builder.build()
    }
}

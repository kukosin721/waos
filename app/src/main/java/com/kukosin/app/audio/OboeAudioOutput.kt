package com.kukosin.app.audio

import android.media.AudioDeviceInfo
import androidx.media3.common.C
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.analytics.PlayerId
import androidx.media3.exoplayer.audio.AudioOutput
import com.kukosin.app.playback.HardwareVolume
import com.kukosin.app.playback.OutputMode
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArraySet

/**
 * [AudioOutput] de Media3 servido por el motor de Oboe.
 *
 * Todo lo que entra por [write] es PCM ya decodificado (y, si el usuario lo pidió,
 * ya ecualizado). Acá no se convierte, no se resamplea y no se aplica ganancia
 * salvo que el volumen esté por debajo de 100 %.
 */
@UnstableApi
class OboeAudioOutput(
    private val engine: AudioEngine,
    private val sampleRate: Int,
    private val mode: OutputMode,
    private val appContext: android.content.Context?,
) : AudioOutput {

    private val listeners = CopyOnWriteArraySet<AudioOutput.Listener>()

    private var started = false
    private var stalled = false
    private var released = false
    private var volume = 1f

    override fun play() {
        val result = engine.start()
        if (result.isOk) {
            started = true
            stalled = false
            val nowMs = android.os.SystemClock.elapsedRealtime()
            listeners.forEach { it.onPositionAdvancing(nowMs) }
        }
    }

    override fun pause() {
        engine.pause()
    }

    override fun write(
        buffer: ByteBuffer,
        encodedAccessUnitCount: Int,
        presentationTimeUs: Long,
    ): Boolean {
        val remaining = buffer.remaining()
        if (remaining == 0) return true

        val accepted = engine.writePacked(buffer).coerceIn(0, remaining)
        buffer.position(buffer.position() + accepted)

        val complete = accepted == remaining
        stalled = !complete
        if (!complete && accepted == 0) {
            // Si el stream murió (DAC desconectado, HAL que lo retiró), devolver false
            // dejaría al sink esperando para siempre. Se lanza WriteException y Media3
            // reporta el error: PlaybackService reconstruye el player en la ruta nueva.
            val code = engine.errorCode()
            if (code != 0) {
                throw AudioOutput.WriteException(code, true)
            }
        }
        return complete
    }

    override fun flush() {
        engine.flush()
        started = false
        stalled = false
    }

    override fun stop() {
        engine.stop()
        stalled = false
    }

    override fun release() {
        if (released) return
        released = true
        engine.close()
        AudioEngineStatus.detach()
        listeners.forEach { it.onReleased() }
    }

    override fun setVolume(volume: Float) {
        this.volume = volume
        when {
            // DAC USB: volumen fijo, se regula en el DAC. La señal no se toca.
            mode.locksVolume -> engine.setVolume(1f)
            // Integrado exclusivo: atenuación digital (bit-perfect sólo al 100 %).
            mode.usesDigitalVolume -> engine.setVolume(volume)
            // Controlador directo: el volumen va al hardware, la muestra queda intacta.
            else -> {
                engine.setVolume(1f)
                HardwareVolume.set(appContext, volume)
            }
        }
    }

    override fun isOffloadedPlayback(): Boolean = false

    override fun getAudioSessionId(): Int = C.AUDIO_SESSION_ID_UNSET

    override fun getSampleRate(): Int = sampleRate

    override fun getBufferSizeInFrames(): Long {
        val info = engine.info()
        val frames = when {
            info.bufferFrames > 0 -> info.bufferFrames
            info.framesPerBurst > 0 -> info.framesPerBurst * 4
            else -> 0
        }
        return frames.toLong()
    }

    override fun getPositionUs(): Long {
        if (sampleRate <= 0) return 0L
        return engine.sourceFramesRendered * C.MICROS_PER_SECOND / sampleRate
    }

    override fun getPlaybackParameters(): PlaybackParameters = PlaybackParameters.DEFAULT

    override fun isStalled(): Boolean = stalled

    override fun addListener(listener: AudioOutput.Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: AudioOutput.Listener) {
        listeners.remove(listener)
    }

    override fun setPlaybackParameters(playbackParameters: PlaybackParameters) {
        // Velocidad/pitch rompería el bit-perfect: no se soporta.
    }

    override fun setOffloadDelayPadding(delayInFrames: Int, paddingInFrames: Int) = Unit

    override fun setOffloadEndOfStream() = Unit

    override fun setPlayerId(playerId: PlayerId) = Unit

    override fun attachAuxEffect(effectId: Int) = Unit

    override fun setAuxEffectSendLevel(level: Float) = Unit

    override fun setPreferredDevice(preferredDevice: AudioDeviceInfo?) {
        // El dispositivo se fija al abrir el stream (AudioRoute → deviceId).
    }
}

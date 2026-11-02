package com.kukosin.app.audio

import android.media.AudioDeviceInfo
import androidx.media3.common.C
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.AudioOutput
import com.kukosin.app.playback.HardwareVolume
import com.kukosin.app.playback.UsbDacManager
import com.kukosin.app.playback.OutputMode
import java.nio.ByteBuffer
import java.util.concurrent.CopyOnWriteArraySet

/**
 * [AudioOutput] de Media3 servido por el motor nativo (AAudio/Oboe).
 *
 * Todo lo que entra por [write] es PCM ya decodificado (y, si el usuario lo pidió,
 * ya ecualizado). Acá no se convierte, no se resamplea y no se aplica ganancia
 * salvo que el volumen esté por debajo de 100 %.
 *
 * Las tres reglas del contrato de Media3 que este adaptador respeta y que antes no
 * se cumplían:
 *
 *  1. [stop] significa "deja sonar lo que queda": espera al drenado del motor.
 *     Devolver el control con audio pendiente corta muestras al final de cada pista
 *     y rompe el gapless.
 *  2. [getPositionUs] es la posición de **playout** (timestamp de AAudio), no los
 *     frames que el callback consumió: si no, el sink cree que terminó antes de
 *     tiempo y adelanta seeks/transiciones.
 *  3. [write] devuelve `false` cuando no entra todo: Media3 reenvía exactamente el
 *     tramo pendiente del mismo buffer.
 */
@UnstableApi
class OboeAudioOutput(
    private val engine: AudioEngine,
    private val sampleRate: Int,
    private val mode: OutputMode,
    private val appContext: android.content.Context?,
    /** true = vaciar sin esperar las transiciones de estado del HAL (menos corte al saltar). */
    private val fastTransitions: Boolean = true,
) : AudioOutput {

    private val listeners = CopyOnWriteArraySet<AudioOutput.Listener>()

    private var started = false
    private var stalled = false
    private var released = false
    private var volume = 1f
    private var pendingCallbackFrames = 0L
    private var lastPositionUs = 0L

    init {
        // Abrir el volumen de hardware del DAC al crear la salida: es una lectura de dos
        // bytes por control request (no bloquea) y hay que hacerlo antes del primer
        // `setVolume`, que llega en cuanto Media3 conoce el volumen del reproductor.
        val context = appContext
        if (mode.locksVolume && context != null && !UsbDacManager.hasHardwareVolume()) {
            runCatching { UsbDacManager.openHardwareVolume(context) }
        }
    }

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
        // El motor aplica un fundido corto y se detiene solo; los datos quedan en el
        // ring para reanudar exactamente donde estaba.
    }

    override fun write(
        buffer: ByteBuffer,
        encodedAccessUnitCount: Int,
        presentationTimeUs: Long,
    ): Boolean {
        val remaining = buffer.remaining()
        if (remaining == 0) return true

        // La apertura del stream es diferida: si falló, hay que fallar YA. Devolver
        // `false` sin consumir deja a Media3 reintentando la misma escritura para
        // siempre (y durante ese tiempo el buffer del player crece sin sonar).
        if (engine.errorCode() != 0 && engine.info().let { !it.opened }) {
            throw AudioOutput.WriteException(engine.errorCode(), true)
        }

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

    /**
     * Descarta lo pendiente. El motor deja el stream en PAUSED (no FLUSHED) para que
     * Media3 pueda reutilizar este mismo [AudioOutput] después de un seek.
     */
    override fun flush() {
        // Entre pistas (transición) se usa el flush que no espera al HAL: media3 llama a
        // `flush()` justo antes de escribir la primera muestra de la pista nueva, así que
        // cada milisegundo que se espere acá es silencio audible. El flush que espera las
        // transiciones de estado se reserva para el ajuste manual (rebuild del player).
        val result = if (fastTransitions) engine.flushForTransition() else engine.flush()
        if (!result.isOk && result != NativeResult.ERROR_INVALID_STATE) {
            // Un flush que no se pudo pedir no invalida el stream: el motor ya descartó sus
            // propios datos y el HAL va a sonar lo que tenía (a lo sumo, el buffer actual).
            AudioEngineStatus.error("Flush fell back to a partial reset (${result.description})")
        }
        started = false
        stalled = false
        lastPositionUs = 0L
        pendingCallbackFrames = 0L
    }

    /**
     * Fin de stream. Media3 lo llama desde `playToEndOfStream()` y espera que el
     * audio encolado suene: el motor drena el ring y se detiene solo.
     *
     * El `drain()` está acotado por timeout, así que no puede colgar el hilo de audio.
     */
    override fun stop() {
        engine.stopAndWait(END_OF_STREAM_DRAIN_TIMEOUT_MS)
        stalled = false
    }

    override fun release() {
        if (released) return
        released = true
        engine.close()
        // El atenuador del DAC se restaura al nivel que tenía: si no, queda bajado para
        // Android y para las demás apps.
        if (mode.locksVolume && appContext != null) {
            runCatching { UsbDacManager.closeHardwareVolume() }
        }
        AudioEngineStatus.detach()
        listeners.forEach { it.onReleased() }
    }

    override fun setVolume(volume: Float) {
        this.volume = volume
        when {
            // DAC USB: el volumen se aplica en el **atenuador del propio DAC** (Feature Unit
            // de USB Audio Class). Antes se fijaba en 1.0 y el control de volumen no hacía
            // nada: la señal quedaba intacta pero no se podía bajar. Ahora, si el DAC expone
            // el control, la muestra sigue sin tocarse y el volumen se mueve en hardware;
            // si no lo expone, se deja al 100 % (nunca se escala la señal en modo bit-perfect).
            mode.locksVolume -> {
                engine.setVolume(1f)
                if (appContext != null && UsbDacManager.hasHardwareVolume()) {
                    UsbDacManager.setHardwareVolume(volume)
                }
            }
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

    /**
     * Posición de playout en microsegundos. El motor la calcula con
     * `AAudioStream_getTimestamp` (frames consumidos menos la latencia real del
     * hardware) y la mantiene monótona dentro del stream.
     */
    override fun getPositionUs(): Long {
        if (sampleRate <= 0) return 0L
        val raw = engine.positionMicros()
        // Nunca retrocede: un timestamp viejo del HAL haría saltar el reloj del player.
        if (raw < lastPositionUs) return lastPositionUs
        lastPositionUs = raw
        return raw
    }

    override fun getPlaybackParameters(): PlaybackParameters = PlaybackParameters.DEFAULT

    override fun setPlaybackParameters(playbackParameters: PlaybackParameters) {}

    override fun setOffloadDelayPadding(delaySamples: Int, paddingSamples: Int) {}

    override fun setOffloadEndOfStream() {}

    override fun attachAuxEffect(auxEffectId: Int) {}

    override fun setAuxEffectSendLevel(level: Float) {}

    override fun setPreferredDevice(audioDeviceInfo: AudioDeviceInfo?) {}

    override fun isStalled(): Boolean = stalled

    override fun addListener(listener: AudioOutput.Listener) {
        listeners.add(listener)
    }

    override fun removeListener(listener: AudioOutput.Listener) {
        listeners.remove(listener)
    }

    private companion object {
        /**
         * Media3 llama a [stop] una vez por stream; si el DAC no consume (p. ej. la
         * app está en segundo plano y el dispositivo se durmió), no se puede colgar
         * el hilo de audio indefinidamente.
         */
        const val END_OF_STREAM_DRAIN_TIMEOUT_MS = 750
    }
}

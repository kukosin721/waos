package com.kukosin.app.playback

import android.content.Context
import android.media.AudioManager
import android.os.Build
import kotlin.math.roundToInt

/**
 * Volumen en el hardware: mueve el índice del stream del sistema, que en un DAC USB
 * termina en el control de volumen del propio DAC (HID), no en la señal digital.
 *
 * Es lo que usan los modos "controlador directo": la muestra nunca se multiplica.
 */
object HardwareVolume {

    fun set(context: Context?, fraction: Float) {
        val audioManager = context?.getSystemService(AudioManager::class.java) ?: return
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val min = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            audioManager.getStreamMinVolume(AudioManager.STREAM_MUSIC)
        } else {
            0
        }
        if (max <= min) return
        val index = (min + fraction.coerceIn(0f, 1f) * (max - min)).roundToInt()
        runCatching { audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, index, 0) }
    }

    /** Nivel actual del hardware (0..1), para mostrarlo en la UI. */
    fun current(context: Context?): Float {
        val audioManager = context?.getSystemService(AudioManager::class.java) ?: return 1f
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val min = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            audioManager.getStreamMinVolume(AudioManager.STREAM_MUSIC)
        } else {
            0
        }
        if (max <= min) return 1f
        val index = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        return ((index - min).toFloat() / (max - min)).coerceIn(0f, 1f)
    }
}

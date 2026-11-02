@file:Suppress("unused", "UNUSED_PARAMETER")
package com.kukosin.app.playback
import android.content.Context
import android.media.AudioDeviceInfo
import com.kukosin.app.audio.AudioEngine

enum class OutputMode(val label: String) {
    DISABLED("Disabled") {
        override val requiresExclusive = false
        override val requiresUsb = false
        override val targetsUsb = false
        override val locksVolume = false
        override val usesDigitalVolume = false
    },
    USB_DAC("USB") {
        override val requiresExclusive = true
        override val requiresUsb = true
        override val targetsUsb = true
        override val locksVolume = true
        override val usesDigitalVolume = false
    },
    DIRECT_DRIVER("Direct") {
        override val requiresExclusive = true
        override val requiresUsb = false
        override val targetsUsb = true
        override val locksVolume = false
        override val usesDigitalVolume = false
    },
    AAUDIO_EXCLUSIVE("AAudio") {
        override val requiresExclusive = true
        override val requiresUsb = false
        override val targetsUsb = false
        override val locksVolume = false
        override val usesDigitalVolume = true
    };
    abstract val requiresExclusive: Boolean
    abstract val requiresUsb: Boolean
    abstract val targetsUsb: Boolean
    abstract val locksVolume: Boolean
    abstract val usesDigitalVolume: Boolean
}

class FakeAudioConfig(
    val strictBitPerfect: Boolean = false,
    val bufferMs: Int = 250,
    val fades: Boolean = true,
)

class SimpleFlow<T>(private val initial: T) { val value: T get() = initial }

class FakeDecoderConfig(
    val decoder: String = "FFmpeg (Lossless)",
    val strictBitPerfect: Boolean = false,
    val bufferMs: Int = 100,
    val fades: Boolean = true,
    val prerollMs: Int = 6,
    val volumeCurve: Int = 1,
    val fastTransitions: Boolean = true,
)

object AudioSettingsManager {
    val configState = SimpleFlow(FakeDecoderConfig())
}

class AudioRouteInfo { val isBuiltin: Boolean = true }

object AudioRoute {
    val current = SimpleFlow(AudioRouteInfo())
    var appContext: Context? = null
    fun deviceFor(mode: OutputMode): AudioDeviceInfo? = null
}

object HardwareVolume { fun set(context: Context?, volume: Float) {} }

object BitPerfectMixer { fun apply(route: Any?, sampleRate: Int, encoding: Int, channelMask: Int) {} }


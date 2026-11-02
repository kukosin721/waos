@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.media3.exoplayer.audio
import androidx.media3.common.Format
import androidx.media3.common.PlaybackParameters
import java.nio.ByteBuffer

interface AudioOutput {
    class WriteException(val errorCode: Int, val isRecoverable: Boolean) : Exception()
    interface Listener {
        fun onPositionAdvancing(eventTimeMs: Long) {}
        fun onReleased() {}
    }
    fun play()
    fun pause()
    fun write(buffer: ByteBuffer, encodedAccessUnitCount: Int, presentationTimeUs: Long): Boolean
    fun flush()
    fun stop()
    fun release()
    fun setVolume(volume: Float)
    fun isOffloadedPlayback(): Boolean
    fun getAudioSessionId(): Int
    fun getSampleRate(): Int
    fun getBufferSizeInFrames(): Long
    fun getPositionUs(): Long
    fun getPlaybackParameters(): PlaybackParameters
    fun isStalled(): Boolean
    fun addListener(listener: Listener)
    fun removeListener(listener: Listener)
}

interface AudioOutputProvider {
    class FormatConfig(val format: Format)
    class OutputConfig {
        val encoding: Int = 0
        val sampleRate: Int = 0
        val channelMask: Int = 0
        class Builder {
            fun setEncoding(v: Int) = this
            fun setSampleRate(v: Int) = this
            fun setChannelMask(v: Int) = this
            fun build(): OutputConfig = OutputConfig()
        }
    }
    class FormatSupport {
        class Builder {
            fun setFormatSupportLevel(level: Int) = this
            fun build(): FormatSupport = FormatSupport()
        }
        companion object {
            val UNSUPPORTED: FormatSupport = FormatSupport()
        }
    }
    class InitializationException(cause: Throwable) : Exception(cause)
    companion object {
        const val FORMAT_SUPPORTED_DIRECTLY: Int = 4
    }
    interface Listener
    fun getFormatSupport(formatConfig: FormatConfig): FormatSupport
    fun getOutputConfig(formatConfig: FormatConfig): OutputConfig
    fun getAudioOutput(config: OutputConfig): AudioOutput
    fun addListener(listener: Listener)
    fun removeListener(listener: Listener)
    fun release()
}



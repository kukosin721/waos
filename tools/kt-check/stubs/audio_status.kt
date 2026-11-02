@file:Suppress("unused", "UNUSED_PARAMETER")
package com.kukosin.app.audio
import com.kukosin.app.playback.OutputMode

object AudioEngineStatus {
    fun attach(engine: AudioEngine, label: String, mode: OutputMode, builtinSpeaker: Boolean) {}
    fun detach() {}
    fun error(message: String) {}
}

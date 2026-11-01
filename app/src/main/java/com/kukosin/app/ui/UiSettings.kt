package com.kukosin.app.ui

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.ui.MotionDurationScale
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Velocidad global de las animaciones de Compose. */
enum class AnimationSpeed(val label: String, val scale: Float) {
    OFF("Off", 0f),
    SLOW("Slow", 0.6f),
    NORMAL("Normal", 1f),
    FAST("Fast", 1.4f),
}

/** Peso base de la tipografía de toda la app. */
enum class FontWeightOption(val label: String) {
    DEFAULT("Default"),
    BOLD("Bold"),
}

data class UiSettings(
    val animationSpeed: AnimationSpeed = AnimationSpeed.NORMAL,
    val fontOption: FontWeightOption = FontWeightOption.DEFAULT,
)

/**
 * Preferencias visuales globales.
 *
 * [motionDurationScale] se instala en el contexto del Recomposer (ver MainActivity), así
 * que TODAS las animaciones de Compose respetan la velocidad elegida sin tocar cada
 * `tween`/`spring` del proyecto.
 */
object UiSettingsManager {

    private const val PREFS_NAME = "kukosin_ui_settings"
    private const val KEY_ANIMATION_SPEED = "animation_speed"
    private const val KEY_FONT_WEIGHT = "font_weight"

    private var prefs: SharedPreferences? = null

    private val state = MutableStateFlow(UiSettings())
    val settings: StateFlow<UiSettings> = state.asStateFlow()

    private class MutableMotionDurationScale(
        override var scaleFactor: Float,
    ) : MotionDurationScale

    private val motionScale = MutableMotionDurationScale(AnimationSpeed.NORMAL.scale)

    val motionDurationScale: MotionDurationScale get() = motionScale

    fun init(context: Context) {
        if (prefs != null) return
        val preferences = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs = preferences
        val speed = runCatching {
            AnimationSpeed.valueOf(
                preferences.getString(KEY_ANIMATION_SPEED, AnimationSpeed.NORMAL.name)
                    ?: AnimationSpeed.NORMAL.name,
            )
        }.getOrDefault(AnimationSpeed.NORMAL)
        val font = runCatching {
            FontWeightOption.valueOf(
                preferences.getString(KEY_FONT_WEIGHT, FontWeightOption.DEFAULT.name)
                    ?: FontWeightOption.DEFAULT.name,
            )
        }.getOrDefault(FontWeightOption.DEFAULT)
        state.value = UiSettings(animationSpeed = speed, fontOption = font)
        motionScale.scaleFactor = speed.scale
    }

    fun setAnimationSpeed(speed: AnimationSpeed) {
        prefs?.edit()?.putString(KEY_ANIMATION_SPEED, speed.name)?.apply()
        motionScale.scaleFactor = speed.scale
        state.value = state.value.copy(animationSpeed = speed)
    }

    fun setFontOption(option: FontWeightOption) {
        prefs?.edit()?.putString(KEY_FONT_WEIGHT, option.name)?.apply()
        state.value = state.value.copy(fontOption = option)
    }
}

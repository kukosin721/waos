package com.kukosin.app.playback

/**
 * Audio output path. Four routes with distinct semantics, not four interchangeable
 * "drivers":
 *
 * | Mode              | Path                          | Volume       | Bit-perfect |
 * |-------------------|-------------------------------|--------------|-------------|
 * | DISABLED          | Android mixer                 | free         | never       |
 * | DIRECT_DRIVER     | exclusive + hardware volume   | on the DAC   | with DAC    |
 * | AAUDIO_EXCLUSIVE  | exclusive + digital volume    | in the app   | only at 100 % |
 * | USB_DAC           | exclusive, track format       | fixed at 100 % | yes       |
 */
enum class OutputMode(
    val label: String,
    val shortLabel: String,
    val description: String,
) {
    DISABLED(
        label = "Disabled",
        shortLabel = "Mixer",
        description = "Goes through the Android mixer: maximum compatibility, but the " +
            "system may resample or apply effects. Not bit-perfect.",
    ),
    USB_DAC(
        label = "USB DAC",
        shortLabel = "USB DAC",
        description = "Opens the USB DAC at each track's sample rate and bit depth, " +
            "without mixing or resampling. Volume is fixed: adjust it on the DAC.",
    ),
    DIRECT_DRIVER(
        label = "Built-in DAC · direct driver",
        shortLabel = "Direct",
        description = "The app opens the headphone/line output exclusively and volume " +
            "is adjusted in hardware, so the signal is not digitally attenuated. " +
            "If a USB DAC is connected, it requests permission to take it over.",
    ),
    AAUDIO_EXCLUSIVE(
        label = "Built-in DAC · AAudio exclusive",
        shortLabel = "AAudio excl.",
        description = "Opens the built-in output (jack or line) exclusively at each " +
            "track's sample rate. Volume is digital: bit-perfect only at 100 %.",
    ),
    ;

    /** ¿Usa el motor de Oboe? (DISABLED va por AudioTrack / mezclador del sistema). */
    val usesEngine: Boolean get() = this != DISABLED

    /** Modos que exigen stream exclusivo: si el HAL no lo da, se informa el error. */
    val requiresExclusive: Boolean get() = this != DISABLED

    /** Sólo se puede elegir si hay un DAC conectado (el mezclador siempre está disponible). */
    val requiresDac: Boolean get() = this != DISABLED

    /** Volumen fijo al 100 %: la señal no se toca ni se atenúa. */
    val locksVolume: Boolean get() = this == USB_DAC

    /** El volumen se aplica en la app (atenuación digital). */
    val usesDigitalVolume: Boolean get() = this == AAUDIO_EXCLUSIVE

    /** El volumen se aplica sobre el dispositivo (hardware). */
    val usesHardwareVolume: Boolean get() = this == DIRECT_DRIVER || this == USB_DAC

    /** A qué dispositivo apunta el modo. */
    val targetsUsb: Boolean get() = this == USB_DAC || this == DIRECT_DRIVER

    val needsUsbPermission: Boolean get() = this == DIRECT_DRIVER

    /** Sin DAC USB este modo no tiene sentido (no hay a dónde abrir el stream). */
    val requiresUsb: Boolean get() = this == USB_DAC

    val volumeHint: String
        get() = when {
            locksVolume -> "Fixed volume at 100 % · adjust it on the DAC"
            usesHardwareVolume -> "Volume in the DAC hardware"
            usesDigitalVolume -> "Digital volume (bit-perfect only at 100 %)"
            else -> "System volume"
        }

    companion object {
        val entriesInOrder: List<OutputMode> =
            listOf(DISABLED, USB_DAC, DIRECT_DRIVER, AAUDIO_EXCLUSIVE)

        fun fromName(name: String?): OutputMode =
            entries.firstOrNull { it.name == name } ?: DISABLED
    }
}

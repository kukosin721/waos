package com.kukosin.app.playback

/**
 * Modo de salida de audio. Son cuatro caminos con semántica distinta, no cuatro
 * "drivers" intercambiables:
 *
 * | Modo              | Camino                        | Volumen      | Bit-perfect |
 * |-------------------|-------------------------------|--------------|-------------|
 * | DISABLED          | mezclador de Android          | libre        | nunca       |
 * | DIRECT_DRIVER     | exclusivo + volumen hardware  | en el DAC    | con DAC     |
 * | AAUDIO_EXCLUSIVE  | exclusivo + volumen digital   | en la app    | sólo al 100 % |
 * | USB_DAC           | exclusivo, formato de la pista| fijo al 100 %| sí          |
 */
enum class OutputMode(
    val label: String,
    val shortLabel: String,
    val description: String,
) {
    DISABLED(
        label = "Desactivado",
        shortLabel = "Mezclador",
        description = "Pasa por el mezclador de Android: máxima compatibilidad, pero el " +
            "sistema puede remuestrear o aplicar efectos. No es bit-perfect.",
    ),
    DIRECT_DRIVER(
        label = "DAC integrado · controlador directo",
        shortLabel = "Directo",
        description = "La app abre la salida de auriculares/línea en exclusivo y el volumen " +
            "se ajusta en el hardware, así que la señal no se atenúa digitalmente. " +
            "Si hay un DAC USB conectado, pide permiso para tomarlo.",
    ),
    AAUDIO_EXCLUSIVE(
        label = "DAC integrado · AAudio exclusivo",
        shortLabel = "AAudio excl.",
        description = "Abre la salida integrada (jack o línea) en exclusivo con la frecuencia " +
            "de cada pista. El volumen es digital: es bit-perfect sólo al 100 %.",
    ),
    USB_DAC(
        label = "DAC USB",
        shortLabel = "USB DAC",
        description = "Abre el DAC USB con la frecuencia y la profundidad de bits de cada pista, " +
            "sin mezclar ni remuestrear. El volumen queda fijo: se regula en el DAC.",
    ),
    ;

    /** ¿Usa el motor de Oboe? (DISABLED va por AudioTrack / mezclador del sistema). */
    val usesEngine: Boolean get() = this != DISABLED

    /** Modos que exigen stream exclusivo: si el HAL no lo da, se informa el error. */
    val requiresExclusive: Boolean get() = this != DISABLED

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
            locksVolume -> "Volumen fijo al 100 % · se regula en el DAC"
            usesHardwareVolume -> "Volumen en el hardware del DAC"
            usesDigitalVolume -> "Volumen digital (bit-perfect sólo al 100 %)"
            else -> "Volumen del sistema"
        }

    companion object {
        val entriesInOrder: List<OutputMode> =
            listOf(AAUDIO_EXCLUSIVE, USB_DAC, DIRECT_DRIVER, DISABLED)

        fun fromName(name: String?): OutputMode =
            entries.firstOrNull { it.name == name } ?: AAUDIO_EXCLUSIVE
    }
}

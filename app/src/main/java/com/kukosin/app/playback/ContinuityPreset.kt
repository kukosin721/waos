package com.kukosin.app.playback

/**
 * Presets de continuidad: qué se sacrifica para que no haya cortes entre canciones.
 *
 * El corte al adelantar o skippear tiene tres causas distintas y hay que atacar las tres:
 *
 *  1. **El buffer del HAL.** Antes de escuchar la muestra nueva hay que vaciar lo que ya
 *     estaba encolado en el buffer del hardware (lo que suena ahora). Con 250 ms de buffer
 *     eso son hasta 250 ms de audio viejo. No se puede evitar (el DAC ya lo tiene), pero sí
 *     se puede hacer más chico.
 *  2. **El preroll del arranque.** El motor arranca cuando el anillo tiene 2 ráfagas o 20 ms
 *     de audio. En una pista nueva eso es espera pura.
 *  3. **Los fundidos.** El motor aplica 25 ms de subida y 60 ms de bajada. Sin ellos puede
 *     aparecer un clic al cortar una onda por la mitad; con ellos, el arranque "respira".
 *
 * Los presets mueven esos tres parámetros juntos, porque por separado no arreglan nada: un
 * buffer chico con preroll grande sigue tardando, y un preroll chico con buffer grande no se
 * escucha antes.
 */
enum class ContinuityPreset(
    val label: String,
    val description: String,
    /** Buffer objetivo del motor en ms. */
    val bufferMs: Int,
    /** Preroll del arranque en ms (-1 = automático del motor). */
    val prerollMs: Int,
    /** Fundidos de entrada/salida del motor (si están apagados no se enmascara ningún clic). */
    val fades: Boolean,
) {
    /** Lo de antes: mucha estabilidad, cortes perceptibles al cambiar de pista. */
    STABLE(
        label = "Stable",
        description = "Biggest buffer: the most resistance to crackles, but a longer gap when you skip.",
        bufferMs = 250,
        prerollMs = -1,
        fades = true,
    ),

    /** El equilibrio recomendado. */
    BALANCED(
        label = "Balanced",
        description = "100 ms buffer and an immediate start. Good on most phones and USB DACs.",
        bufferMs = 100,
        prerollMs = 6,
        fades = true,
    ),

    /**
     * Para DAC por USB y equipos que no se quejan. El buffer del HAL queda en una ráfaga:
     * el salto se escucha casi en el acto.
     */
    INSTANT(
        label = "Instant",
        description = "Smallest buffer and no preroll wait. Fastest skip; needs a device that keeps up.",
        bufferMs = 40,
        prerollMs = 0,
        fades = false,
    );

    companion object {
        fun fromName(name: String?): ContinuityPreset =
            entries.firstOrNull { it.name == name } ?: BALANCED

        /** El preset que corresponde a una combinación guardada (por si se editó a mano). */
        fun matching(bufferMs: Int, fades: Boolean): ContinuityPreset =
            entries.firstOrNull { it.bufferMs == bufferMs && it.fades == fades } ?: BALANCED
    }
}

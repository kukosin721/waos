package com.kukosin.app.eq

import kotlin.math.abs
import kotlin.math.pow

/**
 * Motor de ecualización.
 *
 * Diseño pensado para audio en tiempo real:
 *
 * - **Cascada de biquads en forma directa II traspuesta** (TDF-II). Es la forma con
 *   mejor relación señal/ruido en coma flotante cuando los polos están cerca del
 *   círculo unitario (frecuencias bajas), que es justo el caso crítico de un EQ.
 * - **Cálculo en Double**, salida al formato del decoder al final. Sin cuantización
 *   intermedia entre bandas.
 * - **Suavizado de parámetros** por bloques de [SUB_BLOCK] frames con constante de
 *   tiempo ~15 ms: mover un slider no produce "zipper noise" ni chasquidos, porque
 *   los coeficientes se actualizan a ritmo de audio (750 veces por segundo a 48 kHz)
 *   y no de golpe.
 * - **Dry/Wet** para que encender/apagar el EQ no haga "pop": se cruza la señal seca
 *   con la procesada en ~15 ms.
 * - **Bypass real y bit-exacto**: mientras el cruce está en seco, [isBypass] es true y
 *   el procesador no toca la muestra (copia de bytes idéntica).
 * - **Protección de denormales** en los estados del filtro.
 *
 * No es thread-safe con respecto a [processInterleaved]: solo el hilo de audio lo
 * llama. [requestSettings] se puede llamar desde cualquier hilo (volatile).
 */
internal class EqEngine {

    /** Estado de una biquad: coeficientes + memoria por canal. */
    private class Stage(val channels: Int) {
        var b0 = 1.0
        var b1 = 0.0
        var b2 = 0.0
        var a1 = 0.0
        var a2 = 0.0
        var active = false
        val z1 = DoubleArray(channels)
        val z2 = DoubleArray(channels)
    }

    @Volatile
    private var requested: EqSettings = EqSettings()

    private var sampleRate = 48_000.0
    private var channels = 2
    private var stages: Array<Stage> = emptyArray()
    private var bandFreq = DoubleArray(0)
    private var bandQ = DoubleArray(0)
    private var bandGainTarget = DoubleArray(0)
    private var bandGainCurrent = DoubleArray(0)
    private var bandTypes: Array<EqBandType> = emptyArray()
    private var activeStages = IntArray(0)
    private var activeCount = 0

    private var preampTargetDb = 0.0
    private var preampCurrentDb = 0.0
    private var preampGain = 1.0

    private var wet = 0.0
    private var wetTarget = 0.0

    private var configured = false
    private val designScratch = DoubleArray(5)

    /** ¿La cadena está en bypass total (seco puro, sin ningún proceso)? */
    val isBypass: Boolean
        get() = wet == 0.0 && wetTarget == 0.0

    /** Prepara el motor para un formato de audio nuevo. Llamar desde el hilo de audio. */
    fun configure(sampleRateHz: Int, channelCount: Int) {
        val fs = if (sampleRateHz > 8_000) sampleRateHz.toDouble() else 48_000.0
        val ch = channelCount.coerceIn(1, 8)
        if (configured && fs == sampleRate && ch == channels) return
        sampleRate = fs
        channels = ch
        val s = requested
        val count = s.bands.size.coerceAtMost(MAX_BANDS)
        stages = Array(count) { Stage(ch) }
        bandFreq = DoubleArray(count)
        bandQ = DoubleArray(count)
        bandGainTarget = DoubleArray(count)
        bandGainCurrent = DoubleArray(count)
        bandTypes = Array(count) { i -> s.bands[i].type }
        activeStages = IntArray(count)
        activeCount = 0
        for (i in 0 until count) {
            bandFreq[i] = s.bands[i].freqHz.toDouble()
            bandQ[i] = s.bands[i].q.toDouble()
            bandGainTarget[i] = 0.0
            bandGainCurrent[i] = 0.0
        }
        preampCurrentDb = if (s.isProcessing) s.effectivePreampDb.toDouble() else 0.0
        preampTargetDb = preampCurrentDb
        preampGain = dbToLinear(preampCurrentDb)
        wet = if (s.isProcessing) 1.0 else 0.0
        wetTarget = wet
        resetStates()
        configured = true
        updateCoefficients()
    }

    /** El estado del EQ cambió (cualquier hilo). Se aplica suavemente en el próximo bloque. */
    fun requestSettings(settings: EqSettings) {
        requested = settings
    }

    /** Limpia la memoria de los filtros (seek, cambio de pista, flush). Sin clicks. */
    fun resetStates() {
        for (stage in stages) {
            stage.z1.fill(0.0)
            stage.z2.fill(0.0)
        }
    }

    /**
     * Procesa [buf] (entrelazado, `frames * channels` muestras en el rango -1..1) in place.
     * Devuelve el pico absoluto de la salida (para el vúmetro y la detección de clip).
     */
    fun processInterleaved(buf: DoubleArray, frames: Int, channelCount: Int): Double {
        if (!configured || frames <= 0) return 0.0
        val ch = channelCount.coerceAtMost(channels).coerceAtLeast(1)
        var peak = 0.0
        var frame = 0
        while (frame < frames) {
            val n = (frames - frame).coerceAtMost(SUB_BLOCK)
            smoothAndUpdate()
            val w = wet
            val dry = 1.0 - w
            val g = preampGain
            val running = w > 0.0 && activeCount > 0
            val preampOnly = w > 0.0 && activeCount == 0
            if (!running && !preampOnly) {
                // Seco total: medimos sin tocar nada.
                for (i in 0 until n) {
                    val base = (frame + i) * channelCount
                    for (c in 0 until ch) {
                        val v = buf[base + c]
                        val a = abs(v)
                        if (a > peak) peak = a
                    }
                }
            } else {
                for (i in 0 until n) {
                    val base = (frame + i) * channelCount
                    for (c in 0 until ch) {
                        val x = buf[base + c]
                        var y = x
                        if (running) {
                            for (k in 0 until activeCount) {
                                y = runStage(activeStages[k], y, c)
                            }
                        }
                        y *= g
                        val out = if (w >= 1.0) y else dry * x + w * y
                        buf[base + c] = out
                        val a = abs(out)
                        if (a > peak) peak = a
                    }
                }
            }
            frame += n
        }
        flushDenormals()
        return peak
    }

    // ------------------------------------------------------------------ interno

    private fun runStage(index: Int, input: Double, channel: Int): Double {
        val s = stages[index]
        // Direct Form II transpuesta.
        val y = s.b0 * input + s.z1[channel]
        s.z1[channel] = s.b1 * input - s.a1 * y + s.z2[channel]
        s.z2[channel] = s.b2 * input - s.a2 * y
        return y
    }

    private fun smoothAndUpdate() {
        val target = requested
        val active = target.isProcessing
        wetTarget = if (active) 1.0 else 0.0
        wet += (wetTarget - wet) * SMOOTH

        val count = bandGainCurrent.size
        var changed = false
        if (target.bands.size >= count) {
            for (i in 0 until count) {
                val band = target.bands[i]
                val gTarget = if (active) band.gainDb.toDouble() else 0.0
                if (band.freqHz.toDouble() != bandFreq[i]) {
                    bandFreq[i] = band.freqHz.toDouble()
                    changed = true
                }
                if (band.q.toDouble() != bandQ[i]) {
                    bandQ[i] = band.q.toDouble()
                    changed = true
                }
                if (band.type != bandTypes[i]) {
                    bandTypes[i] = band.type
                    changed = true
                }
                if (abs(gTarget - bandGainTarget[i]) > 1e-6) {
                    bandGainTarget[i] = gTarget
                    changed = true
                }
            }
        }
        for (i in 0 until count) {
            val delta = bandGainTarget[i] - bandGainCurrent[i]
            if (abs(delta) > 1e-4) {
                bandGainCurrent[i] += delta * SMOOTH
                changed = true
            } else if (delta != 0.0) {
                bandGainCurrent[i] = bandGainTarget[i]
                changed = true
            }
        }

        val preampT = if (active) target.effectivePreampDb.toDouble() else 0.0
        if (abs(preampT - preampTargetDb) > 1e-6) {
            preampTargetDb = preampT
            changed = true
        }
        val pDelta = preampTargetDb - preampCurrentDb
        if (abs(pDelta) > 1e-4) {
            preampCurrentDb += pDelta * SMOOTH
            preampGain = dbToLinear(preampCurrentDb)
            changed = true
        } else if (pDelta != 0.0) {
            preampCurrentDb = preampTargetDb
            preampGain = dbToLinear(preampCurrentDb)
            changed = true
        }

        if (abs(wet - wetTarget) < 1e-3) wet = wetTarget

        if (changed) updateCoefficients()
    }

    private fun updateCoefficients() {
        var n = 0
        for (i in stages.indices) {
            val s = stages[i]
            val active = BiquadDesign.designInto(
                type = bandTypes[i],
                sampleRate = sampleRate,
                freqHz = bandFreq[i],
                q = bandQ[i],
                gainDb = bandGainCurrent[i],
                out = designScratch,
            )
            s.b0 = designScratch[0]
            s.b1 = designScratch[1]
            s.b2 = designScratch[2]
            s.a1 = designScratch[3]
            s.a2 = designScratch[4]
            s.active = active
            if (active) {
                if (n < activeStages.size) activeStages[n] = i
                n++
            }
        }
        activeCount = n
    }

    private fun flushDenormals() {
        for (s in stages) {
            for (c in 0 until channels) {
                if (abs(s.z1[c]) < DENORMAL) s.z1[c] = 0.0
                if (abs(s.z2[c]) < DENORMAL) s.z2[c] = 0.0
            }
        }
    }

    private companion object {
        const val MAX_BANDS = 16
        /** 64 frames ≈ 1.33 ms a 48 kHz: los parámetros se mueven a ritmo de audio. */
        const val SUB_BLOCK = 64
        /** One-pole por sub-bloque → constante de tiempo ≈ 15 ms. */
        const val SMOOTH = 0.35
        const val DENORMAL = 1e-20

        fun dbToLinear(db: Double): Double = 10.0.pow(db / 20.0)
    }
}

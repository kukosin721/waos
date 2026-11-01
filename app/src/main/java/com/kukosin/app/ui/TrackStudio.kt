package com.kukosin.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.kukosin.app.data.analysis.AudioAnalysis
import com.kukosin.app.data.analysis.AuthenticityVerdict
import com.kukosin.app.ui.theme.Cream
import com.kukosin.app.ui.theme.CreamMuted
import com.kukosin.app.ui.theme.Gold
import com.kukosin.app.ui.theme.Ink
import com.kukosin.app.ui.theme.InkSoft
import kotlin.math.roundToInt

@Composable
fun AnalysisPanel(
    analysis: AudioAnalysis?,
    modifier: Modifier = Modifier,
    onAnalyzeClick: (() -> Unit)? = null,
    scrollable: Boolean = true,
    /** false para streams de red (YouTube Music): no hay PCM local que analizar. */
    canAnalyze: Boolean = true,
) {
    if (analysis?.isAnalyzing == true) {
        Box(modifier = modifier.fillMaxWidth().height(200.dp), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CircularProgressIndicator(color = Gold)
                Spacer(Modifier.height(12.dp))
                Text("Decodificando PCM y analizando espectro FFT…", color = CreamMuted)
            }
        }
        return
    }

    val hasSpectrum = analysis?.spectrumDb?.isNotEmpty() == true
    val verdictColor = when (analysis?.verdict) {
        AuthenticityVerdict.AUTHENTIC_HIRES, AuthenticityVerdict.AUTHENTIC_CD -> Gold
        AuthenticityVerdict.AUTHENTIC_LOSSY -> Color(0xFF64B5F6)
        AuthenticityVerdict.UPSAMPLED, AuthenticityVerdict.LOSSY_TRANSCODE -> Color(0xFFE07A5F)
        else -> CreamMuted
    }

    @Composable
    fun AnalysisContent() {
        Surface(
            color = InkSoft,
            shape = RoundedCornerShape(16.dp),
            border = androidx.compose.foundation.BorderStroke(1.dp, Gold.copy(alpha = 0.4f)),
        ) {
            Column(Modifier.padding(14.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .background(if (hasSpectrum) verdictColor else Gold, RoundedCornerShape(5.dp)),
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            if (hasSpectrum) analysis?.verdictTitle.orEmpty() else "Verificación de Autenticidad Hi-Res",
                            color = if (hasSpectrum) verdictColor else Gold,
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.titleMedium,
                        )
                    }
                }
                Spacer(Modifier.height(6.dp))
                Text(
                    when {
                        !canAnalyze -> "Este audio llega por streaming desde YouTube Music, así que no hay muestras PCM locales para analizar. El análisis espectral y de autenticidad funciona con archivos del dispositivo (FLAC, WAV, MP3, etc.)."
                        hasSpectrum -> analysis?.verdictDetail.orEmpty()
                        else -> "Analiza las muestras PCM y el espectro de frecuencias FFT para verificar si el archivo es 100% auténtico, si fue upsampleado o si es un transcode de MP3/AAC."
                    },
                    color = Cream,
                    style = MaterialTheme.typography.bodyMedium,
                )
                if (canAnalyze) {
                    Spacer(Modifier.height(12.dp))
                    Button(
                        onClick = { onAnalyzeClick?.invoke() },
                        colors = ButtonDefaults.buttonColors(containerColor = Gold, contentColor = Ink),
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Icon(Icons.Rounded.GraphicEq, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(
                            if (hasSpectrum) "Re-analizar Espectro y Autenticidad" else "🔍 Verificar Autenticidad y Espectro",
                            fontWeight = FontWeight.Bold,
                        )
                    }
                }
            }
        }

        if (hasSpectrum && analysis != null) {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("Espectro de Frecuencia (FFT 4096)", color = Gold, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)
                    Text(
                        "Corte: ${analysis.cutoffHz} Hz",
                        color = Color(0xFFE07A5F),
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                    )
                }
                SpectrumChart(
                    magnitudes = analysis.spectrumDb,
                    frequencies = analysis.spectrumHz,
                    cutoffHz = analysis.cutoffHz.toFloat(),
                    nyquistHz = analysis.nyquistHz.toFloat(),
                )
                Row(
                    modifier = Modifier.fillMaxWidth().padding(top = 2.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text("Pico: ${"%.1f".format(analysis.peakDb)} dBFS", color = CreamMuted, style = MaterialTheme.typography.labelSmall)
                    Text("RMS: ${"%.1f".format(analysis.rmsDb)} dBFS", color = CreamMuted, style = MaterialTheme.typography.labelSmall)
                    Text("DR: ${"%.1f".format(analysis.dynamicRangeDb)} dB", color = Gold, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
                }
            }
        }

        Text("Parámetros Técnicos y Metadatos Extraídos", color = Gold, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold)

        val technicalList = analysis?.technical?.ifEmpty { analysis.tags }.orEmpty()
        technicalList.forEach { (label, value) ->
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(InkSoft.copy(alpha = 0.5f), RoundedCornerShape(6.dp))
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(label, color = CreamMuted, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(0.42f))
                Text(value, color = Cream, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Medium, modifier = Modifier.weight(0.58f))
            }
        }
    }

    if (scrollable) {
        LazyColumn(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            item { AnalysisContent() }
        }
    } else {
        Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            AnalysisContent()
        }
    }
}

@Composable
fun SpectrumChart(
    magnitudes: List<Float>,
    frequencies: List<Float>,
    cutoffHz: Float,
    nyquistHz: Float,
) {
    val textMeasurer = rememberTextMeasurer()
    val labelStyle = TextStyle(color = CreamMuted.copy(alpha = 0.75f), fontSize = 9.sp)

    Canvas(
        modifier = Modifier
            .fillMaxWidth()
            .height(210.dp)
            .background(Ink, RoundedCornerShape(12.dp))
            .padding(6.dp),
    ) {
        if (magnitudes.isEmpty()) return@Canvas
        val minDb = -90f
        val maxDb = magnitudes.max().coerceAtLeast(-10f)
        val barWidth = size.width / magnitudes.size

        fun yForDb(db: Float): Float =
            ((db - minDb) / (maxDb - minDb)).coerceIn(0f, 1f) * size.height

        // Rejilla horizontal cada 20 dB.
        var gridDb = -20f
        while (gridDb > minDb) {
            val y = size.height - yForDb(gridDb)
            drawLine(
                color = CreamMuted.copy(alpha = 0.08f),
                start = Offset(0f, y),
                end = Offset(size.width, y),
                strokeWidth = 1f,
            )
            gridDb -= 20f
        }

        // Barras: redondeadas y con la opacidad según la intensidad, así los picos se
        // leen de un vistazo en vez de verse todas iguales.
        magnitudes.forEachIndexed { index, db ->
            val h = yForDb(db) * size.height
            if (h <= 0.5f) return@forEachIndexed
            val below = frequencies.getOrNull(index)?.let { it <= cutoffHz } == true
            val intensity = ((db - minDb) / (maxDb - minDb)).coerceIn(0f, 1f)
            val color = if (below) {
                Gold.copy(alpha = 0.35f + 0.6f * intensity)
            } else {
                CreamMuted.copy(alpha = 0.18f + 0.30f * intensity)
            }
            drawRoundRect(
                color = color,
                topLeft = Offset(index * barWidth, size.height - h),
                size = Size(
                    (barWidth * 0.72f).coerceAtLeast(1f),
                    h.coerceAtLeast(1f),
                ),
                cornerRadius = CornerRadius(
                    (barWidth * 0.36f).coerceAtLeast(0.5f),
                    (barWidth * 0.36f).coerceAtLeast(0.5f),
                ),
            )
        }

        fun xForHz(hz: Float): Float {
            if (nyquistHz <= 0f) return 0f
            return (hz / nyquistHz).coerceIn(0f, 1f) * size.width
        }

        if (cutoffHz > 0f) {
            val x = xForHz(cutoffHz)
            drawLine(Color(0xFFE07A5F), Offset(x, 0f), Offset(x, size.height), strokeWidth = 3f)
        }

        if (nyquistHz > 20000f) {
            val x = xForHz(20000f)
            drawLine(CreamMuted.copy(alpha = 0.5f), Offset(x, 0f), Offset(x, size.height), strokeWidth = 1.5f)
            val mark = textMeasurer.measure("20 kHz", labelStyle)
            drawText(mark, topLeft = Offset((x + 3f).coerceAtMost(size.width - mark.size.width), 2f))
        }

        // Referencias de eje: 0 Hz a la izquierda y Nyquist a la derecha.
        val zero = textMeasurer.measure("0 Hz", labelStyle)
        drawText(zero, topLeft = Offset(0f, size.height - zero.size.height - 1f))
        if (nyquistHz > 0f) {
            val label = "Nyquist ${(nyquistHz / 1000f).roundToInt()}k"
            val nyq = textMeasurer.measure(label, labelStyle)
            drawText(
                nyq,
                topLeft = Offset(
                    (size.width - nyq.size.width).coerceAtLeast(0f),
                    size.height - nyq.size.height - 1f,
                ),
            )
        }
    }
}

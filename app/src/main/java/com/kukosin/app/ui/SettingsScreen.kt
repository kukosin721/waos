@file:OptIn(UnstableApi::class)

package com.kukosin.app.ui

import androidx.media3.common.util.UnstableApi
import android.Manifest
import android.content.ComponentName
import android.net.Uri
import android.util.Log
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.animation.core.animate
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.NavigateNext
import androidx.compose.material.icons.rounded.Album
import androidx.compose.material.icons.rounded.Cloud
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Headphones
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.rememberTransition
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.material.icons.automirrored.rounded.PlaylistAdd
import androidx.compose.material.icons.automirrored.rounded.QueueMusic
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.SkipNext
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import com.kukosin.app.ui.component.KukosinWaveformSlider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.withFrameNanos
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import com.kukosin.app.data.artwork.EmbeddedArtwork
import com.kukosin.app.data.ytm.YtmService
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.BlurredEdgeTreatment
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import coil.compose.AsyncImage
import com.kukosin.app.data.AlbumGroup
import com.kukosin.app.data.ArtistGroup
import com.kukosin.app.data.FolderNode
import com.kukosin.app.data.LibraryInsights
import com.kukosin.app.data.Track
import com.kukosin.app.data.formatTime
import com.kukosin.app.data.ytm.YtmTrack
import com.kukosin.app.audio.AudioEngineStatus
import com.kukosin.app.playback.AudioConfig
import com.kukosin.app.playback.AudioRoute
import com.kukosin.app.playback.OutputMode
import com.kukosin.app.playback.OutputDevice
import com.kukosin.app.playback.PlaybackService
import com.kukosin.app.playback.QueueManager
import com.kukosin.app.playback.UsbDacManager
import com.kukosin.app.ui.theme.Cream
import com.kukosin.app.ui.theme.CreamMuted
import com.kukosin.app.ui.theme.AccentPreset
import com.kukosin.app.ui.theme.AccentTheme
import com.kukosin.app.ui.theme.Gold
import com.kukosin.app.ui.theme.Line
import com.kukosin.app.ui.theme.accentColor
import com.kukosin.app.ui.theme.Ink
import com.kukosin.app.ui.theme.InkElevated
import com.kukosin.app.ui.theme.InkSoft
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
internal fun FullScreenSettingsScreen(
    visibilityState: MutableTransitionState<Boolean>,
    output: OutputDevice,
    autoAnalyze: Boolean,
    onToggleAutoAnalyze: () -> Unit,
    outputMode: OutputMode,
    onSelectOutputMode: (OutputMode) -> Unit,
    strictBitPerfect: Boolean,
    onToggleStrictBitPerfect: () -> Unit,
    fades: Boolean,
    onToggleFades: () -> Unit,
    bufferMs: Int,
    onSelectBufferMs: (Int) -> Unit,
    decoder: String,
    onSelectDecoder: (String) -> Unit,
    usbPermissionGranted: Boolean,
    onRequestUsbPermission: () -> Unit,
    accentHue: Float,
    onPreviewAccent: (Float) -> Unit,
    onSelectAccent: (AccentPreset) -> Unit,
    onCommitAccent: () -> Unit,
    onResetAccent: () -> Unit,
    onRescan: () -> Unit,
    onDismiss: () -> Unit,
) {
    // Estado real del motor nativo: lo que suena, no lo que pedimos.
    val engineStatus by AudioEngineStatus.status.collectAsState()
    val engineInfo = engineStatus.info
    val engineStateText = if (engineInfo.opened) "Abierto · ${engineInfo.apiLabel}" else "Sin stream abierto"
    LaunchedEffect(engineInfo.opened) {
        while (true) {
            AudioEngineStatus.refresh()
            delay(500)
        }
    }

    val transition = rememberTransition(visibilityState, label = "settingsScreen")
    val reveal by transition.animateFloat(
        transitionSpec = {
            tween(durationMillis = if (targetState) 300 else 220, easing = FastOutSlowInEasing)
        },
        label = "settingsReveal",
    ) { visible -> if (visible) 1f else 0f }
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer {
                    alpha = reveal
                    translationY = (1f - reveal) * 32.dp.toPx()
                    val scale = 0.975f + 0.025f * reveal
                    scaleX = scale
                    scaleY = scale
                },
            color = Ink,
            contentColor = Cream,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .statusBarsPadding()
                    .navigationBarsPadding()
                    .padding(horizontal = 20.dp, vertical = 12.dp),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Atrás", tint = Gold, modifier = Modifier.size(28.dp))
                    }
                    Spacer(Modifier.width(8.dp))
                    Column {
                        Text("Configuración de Audio & HiFi", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = Gold)
                        Text("Kukosin High-Resolution Audio Engine", style = MaterialTheme.typography.labelSmall, color = CreamMuted)
                    }
                }

                Spacer(Modifier.height(16.dp))

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    item {
                        Text("Modo de Salida de Audio", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Camino de la señal hasta el DAC", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Cuatro caminos con semántica propia. Sólo los tres exclusivos usan el motor de Oboe, " +
                                        "y el bit-perfect sólo existe con un DAC: el altavoz interno siempre pasa por el " +
                                        "DSP y el amplificador del teléfono.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                                Spacer(Modifier.height(10.dp))
                                OutputMode.entriesInOrder.forEach { mode ->
                                    val selected = outputMode == mode
                                    Surface(
                                        color = if (selected) Gold.copy(alpha = 0.14f) else Ink,
                                        shape = RoundedCornerShape(12.dp),
                                        border = androidx.compose.foundation.BorderStroke(
                                            1.dp,
                                            if (selected) Gold else Cream.copy(alpha = 0.10f),
                                        ),
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .clickable { onSelectOutputMode(mode) },
                                    ) {
                                        Row(
                                            modifier = Modifier.padding(12.dp),
                                            verticalAlignment = Alignment.Top,
                                        ) {
                                            Column(modifier = Modifier.weight(1f)) {
                                                Text(
                                                    mode.label,
                                                    style = MaterialTheme.typography.titleSmall,
                                                    fontWeight = FontWeight.Bold,
                                                    color = if (selected) Gold else Cream,
                                                )
                                                Spacer(Modifier.height(2.dp))
                                                Text(
                                                    mode.description,
                                                    style = MaterialTheme.typography.bodySmall,
                                                    color = CreamMuted,
                                                )
                                                Spacer(Modifier.height(4.dp))
                                                Text(
                                                    mode.volumeHint,
                                                    style = MaterialTheme.typography.labelSmall,
                                                    color = if (selected) Gold else CreamMuted.copy(alpha = 0.7f),
                                                )
                                            }
                                            if (selected) {
                                                Spacer(Modifier.width(8.dp))
                                                Icon(
                                                    Icons.Rounded.Check,
                                                    contentDescription = null,
                                                    tint = Gold,
                                                    modifier = Modifier.size(18.dp),
                                                )
                                            }
                                        }
                                    }
                                    Spacer(Modifier.height(8.dp))
                                }

                                if (outputMode.requiresUsb && !output.isUsb) {
                                    Text(
                                        "No hay DAC USB conectado: en este modo no hay a dónde abrir el stream. " +
                                            "La app sigue sonando, pero no puede ser bit-perfect.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = Color(0xFFFFB26B),
                                    )
                                    Spacer(Modifier.height(8.dp))
                                }

                                if (outputMode.needsUsbPermission && !usbPermissionGranted) {
                                    Text(
                                        "Este modo necesita permiso USB para tomar el DAC.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = Color(0xFFFFB26B),
                                    )
                                    Spacer(Modifier.height(6.dp))
                                    Surface(
                                        color = Gold,
                                        shape = RoundedCornerShape(10.dp),
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .clickable { onRequestUsbPermission() },
                                    ) {
                                        Text(
                                            "Conceder permiso USB",
                                            color = Ink,
                                            fontWeight = FontWeight.Bold,
                                            style = MaterialTheme.typography.labelLarge,
                                            modifier = Modifier.padding(vertical = 10.dp),
                                            textAlign = TextAlign.Center,
                                        )
                                    }
                                    Spacer(Modifier.height(8.dp))
                                }

                                Spacer(Modifier.height(4.dp))
                                SettingsStatusLine("Estado", engineStateText)
                                SettingsStatusLine("API", engineInfo.apiLabel)
                                SettingsStatusLine("Dispositivo", engineStatus.deviceName.ifBlank { output.name })
                                SettingsStatusLine("Stream", engineInfo.streamLabel)
                                SettingsStatusLine(
                                    "Bit-perfect ahora",
                                    when {
                                        engineStatus.bitPerfectNow -> "Sí · exclusivo, sin conversión, volumen 100 %"
                                        engineStatus.mode == OutputMode.DISABLED -> "No · modo Desactivado (mezclador Android)"
                                        engineStatus.builtinSpeaker -> "No · el altavoz interno no puede ser bit-perfect"
                                        engineStatus.eqActive -> "No · ecualizador activo (DSP de 64 bits)"
                                        !engineInfo.opened -> "Sin stream abierto"
                                        !engineInfo.exclusive -> "No · el mezclador comparte la salida"
                                        engineInfo.converted -> "No · convertido al formato del DAC"
                                        engineInfo.volumePercent < 100 -> "No · volumen al ${engineInfo.volumePercent} %"
                                        else -> "No"
                                    },
                                    highlight = engineStatus.bitPerfectNow,
                                )
                                if (engineInfo.opened) {
                                    SettingsStatusLine(
                                        "Buffer",
                                        "${engineInfo.bufferFrames} frames · " +
                                            "${Math.round(engineInfo.bufferMs)} ms · burst ${engineInfo.framesPerBurst}",
                                    )
                                    SettingsStatusLine(
                                        "Xruns",
                                        if (engineInfo.xRuns < 0) "No reportado" else engineInfo.xRuns.toString(),
                                    )
                                    if (engineInfo.xRuns > 0) {
                                        Spacer(Modifier.height(6.dp))
                                        Text(
                                            "Hubo ${engineInfo.xRuns} xrun: el motor se quedó sin datos o el sistema " +
                                                "no lo atendió a tiempo. Si se escucha un chasquido, subí el Buffer de salida.",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = Color(0xFFFFB26B),
                                        )
                                    }
                                }
                                engineStatus.message?.let { message ->
                                    Spacer(Modifier.height(8.dp))
                                    Text(
                                        message,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = Color(0xFFFF6B6B),
                                    )
                                }
                            }
                        }
                    }

                    item {
                        Text("Frecuencias Permitidas por el Hardware DAC", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text(output.name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Gold)
                                Spacer(Modifier.height(4.dp))
                                Text("Frecuencias de muestreo reportadas por la salida activa:", style = MaterialTheme.typography.bodySmall, color = CreamMuted)
                                Spacer(Modifier.height(8.dp))
                                Text(
                                    text = output.sampleRatesLabel.ifBlank { "44.1 kHz, 48.0 kHz, 88.2 kHz, 96.0 kHz, 176.4 kHz, 192.0 kHz, 384.0 kHz" },
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = Cream,
                                    fontWeight = FontWeight.Bold,
                                )
                            }
                        }
                    }

                    item {
                        Text("Bit-Perfect Estricto", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Nunca convertir", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                    Spacer(Modifier.height(4.dp))
                                    Text(
                                        "Si el DAC no acepta el formato exacto, la reproducción falla y avisa en vez de " +
                                            "resamplear por atrás. Apagado (recomendado), intenta bit-perfect primero y " +
                                            "sólo si no se puede convierte con el resampler Kaiser del motor.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = CreamMuted,
                                    )
                                }
                                Spacer(Modifier.width(12.dp))
                                Switch(
                                    checked = strictBitPerfect,
                                    onCheckedChange = { onToggleStrictBitPerfect() },
                                    colors = SwitchDefaults.colors(
                                        checkedThumbColor = Ink,
                                        checkedTrackColor = Gold,
                                        uncheckedThumbColor = CreamMuted,
                                        uncheckedTrackColor = InkSoft,
                                    ),
                                )
                            }
                        }
                    }

                    item {
                        Text("Fundidos de Entrada y Salida", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Fundidos suaves", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                    Spacer(Modifier.height(4.dp))
                                    Text(
                                        "25 ms al arrancar y 60 ms al pausar o detener, más un fundido corto después de " +
                                            "cada salto: sin clics ni cortes secos. Apagalo si querés la señal intacta " +
                                            "desde el primer frame (durante el fundido la ganancia no es 100 %).",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = CreamMuted,
                                    )
                                }
                                Spacer(Modifier.width(12.dp))
                                Switch(
                                    checked = fades,
                                    onCheckedChange = { onToggleFades() },
                                    colors = SwitchDefaults.colors(
                                        checkedThumbColor = Ink,
                                        checkedTrackColor = Gold,
                                        uncheckedThumbColor = CreamMuted,
                                        uncheckedTrackColor = InkSoft,
                                    ),
                                )
                            }
                        }
                    }

                    item {
                        Text("Buffer de Salida", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Buffer interno del motor ($bufferMs ms)", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Es el buffer del motor en memoria de la app, el que absorbe los tirones del " +
                                        "decodificador. El buffer de hardware de AAudio se queda en el mínimo (2 ráfagas) " +
                                        "para que un salto o un desplazamiento sea instantáneo: vaciarlo no cuesta nada.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                                Spacer(Modifier.height(10.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                ) {
                                    AudioConfig.BUFFER_OPTIONS.forEach { option ->
                                        val selected = bufferMs == option
                                        Surface(
                                            color = if (selected) Gold else Ink,
                                            shape = RoundedCornerShape(10.dp),
                                            border = androidx.compose.foundation.BorderStroke(1.dp, Gold),
                                            modifier = Modifier
                                                .weight(1f)
                                                .clickable { onSelectBufferMs(option) },
                                        ) {
                                            Text(
                                                text = "$option ms",
                                                color = if (selected) Ink else Gold,
                                                style = MaterialTheme.typography.labelSmall,
                                                fontWeight = FontWeight.Bold,
                                                modifier = Modifier.padding(vertical = 8.dp),
                                                textAlign = TextAlign.Center,
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }

                    item {
                        Text("Decodificador", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Profundidad real del PCM", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Define con qué profundidad sale el PCM del decoder. El decodificador del sistema " +
                                        "(MediaCodec) entrega la profundidad real del archivo; FFmpeg es más compatible con " +
                                        "formatos raros (APE, DSD, WavPack) pero entrega 16 bits.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                                Spacer(Modifier.height(10.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                ) {
                                    listOf("Automático", "MediaCodec", "FFmpeg (Lossless)").forEach { option ->
                                        val selected = decoder == option
                                        Surface(
                                            color = if (selected) Gold else Ink,
                                            shape = RoundedCornerShape(10.dp),
                                            border = androidx.compose.foundation.BorderStroke(1.dp, Gold),
                                            modifier = Modifier
                                                .weight(1f)
                                                .clickable { onSelectDecoder(option) },
                                        ) {
                                            Text(
                                                text = option,
                                                color = if (selected) Ink else Gold,
                                                style = MaterialTheme.typography.labelSmall,
                                                fontWeight = FontWeight.Bold,
                                                modifier = Modifier.padding(vertical = 8.dp, horizontal = 4.dp),
                                                textAlign = TextAlign.Center,
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }

                    item {
                        Text("Análisis Espectral y Autenticidad", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Análisis Espectral Automático", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                    Spacer(Modifier.height(4.dp))
                                    Text(
                                        "Decodificar muestras PCM y calcular espectro FFT de 4096 puntos automáticamente al reproducir pistas.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = CreamMuted,
                                    )
                                }
                                Spacer(Modifier.width(12.dp))
                                Switch(
                                    checked = autoAnalyze,
                                    onCheckedChange = { onToggleAutoAnalyze() },
                                    colors = SwitchDefaults.colors(
                                        checkedThumbColor = Ink,
                                        checkedTrackColor = Gold,
                                        uncheckedThumbColor = CreamMuted,
                                        uncheckedTrackColor = InkSoft,
                                    ),
                                )
                            }
                        }
                    }

                    item {
                        Text("Biblioteca y Deduplicación", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Deduplicación de Pistas Activa", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Filtrado inteligente por ruta de archivo para evitar canciones duplicadas entre MediaStore Audio y MediaStore Files.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                                Spacer(Modifier.height(12.dp))
                                Button(
                                    onClick = {
                                        onRescan()
                                    },
                                    colors = ButtonDefaults.buttonColors(containerColor = Gold, contentColor = Ink),
                                    shape = RoundedCornerShape(12.dp),
                                    modifier = Modifier.fillMaxWidth(),
                                ) {
                                    Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(18.dp))
                                    Spacer(Modifier.width(8.dp))
                                    Text("Rescanear Biblioteca Completa", fontWeight = FontWeight.Bold)
                                }
                            }
                        }
                    }

                    item {
                        Text("Personalización", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Color de Acento", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "El dorado de fábrica se puede cambiar por otro tono. Se aplica a toda la app y queda guardado.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                                Spacer(Modifier.height(14.dp))
                                LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                    items(AccentPreset.entries.toList()) { preset ->
                                        val selected = AccentTheme.isSelected(preset)
                                        Column(
                                            horizontalAlignment = Alignment.CenterHorizontally,
                                            modifier = Modifier
                                                .clip(RoundedCornerShape(12.dp))
                                                .clickable { onSelectAccent(preset) }
                                                .padding(vertical = 4.dp, horizontal = 2.dp),
                                        ) {
                                            Box(
                                                modifier = Modifier
                                                    .size(36.dp)
                                                    .clip(CircleShape)
                                                    .background(accentColor(preset.hueDegrees))
                                                    .then(
                                                        if (selected) {
                                                            Modifier.border(2.dp, Cream, CircleShape)
                                                        } else {
                                                            Modifier
                                                        },
                                                    ),
                                                contentAlignment = Alignment.Center,
                                            ) {
                                                if (selected) {
                                                    Icon(
                                                        Icons.Rounded.Check,
                                                        contentDescription = null,
                                                        tint = Ink,
                                                        modifier = Modifier.size(18.dp),
                                                    )
                                                }
                                            }
                                            Spacer(Modifier.height(4.dp))
                                            Text(
                                                preset.label,
                                                style = MaterialTheme.typography.labelSmall,
                                                color = if (selected) Cream else CreamMuted,
                                            )
                                        }
                                    }
                                }

                                Spacer(Modifier.height(14.dp))
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                ) {
                                    Text("Tono fino", style = MaterialTheme.typography.labelMedium, color = CreamMuted)
                                    Text(
                                        "${accentHue.toInt()}°",
                                        style = MaterialTheme.typography.labelMedium,
                                        color = Gold,
                                        fontWeight = FontWeight.Bold,
                                    )
                                }
                                Slider(
                                    value = accentHue,
                                    onValueChange = onPreviewAccent,
                                    onValueChangeFinished = onCommitAccent,
                                    valueRange = 0f..360f,
                                    colors = SliderDefaults.colors(
                                        thumbColor = Gold,
                                        activeTrackColor = Gold,
                                        inactiveTrackColor = Line,
                                    ),
                                )
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.End,
                                ) {
                                    Text(
                                        "Volver al oro",
                                        style = MaterialTheme.typography.labelMedium,
                                        color = Gold,
                                        fontWeight = FontWeight.Bold,
                                        modifier = Modifier
                                            .clip(RoundedCornerShape(8.dp))
                                            .clickable { onResetAccent() }
                                            .padding(horizontal = 10.dp, vertical = 6.dp),
                                    )
                                }
                            }
                        }
                    }

                    item {
                        Text("Acerca de Kukosin Player", style = MaterialTheme.typography.labelLarge, color = Gold, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        Surface(
                            color = InkSoft,
                            shape = RoundedCornerShape(16.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Column(modifier = Modifier.padding(16.dp)) {
                                Text("Kukosin HiFi Audio Player v1.0.0", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "Motor de reproducción lossless impulsado por Android Jetpack Media3, decodificadores nativos FFmpeg, NextLib MediaInfo y análisis de espectro FFT.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = CreamMuted,
                                )
                            }
                        }
                    }

                    item {
                        Spacer(Modifier.height(16.dp))
                    }
                }
            }
        }
    }
}

@Composable
/** Fila etiqueta/valor del panel del motor. */
@Composable
internal fun SettingsStatusLine(label: String, value: String, highlight: Boolean = false) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            label,
            style = MaterialTheme.typography.bodySmall,
            color = CreamMuted,
            modifier = Modifier.weight(0.36f),
        )
        Text(
            value,
            style = MaterialTheme.typography.bodySmall,
            color = if (highlight) Gold else Cream,
            fontWeight = if (highlight) FontWeight.Bold else FontWeight.Medium,
            modifier = Modifier.weight(0.64f),
            textAlign = TextAlign.End,
        )
    }
}

internal fun audioPermissions(): Array<String> {
    val permissions = mutableListOf<String>()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        permissions += Manifest.permission.READ_MEDIA_AUDIO
        permissions += Manifest.permission.POST_NOTIFICATIONS
    } else {
        permissions += Manifest.permission.READ_EXTERNAL_STORAGE
    }
    return permissions.toTypedArray()
}

internal fun hasAudioPermission(context: android.content.Context): Boolean {
    val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        Manifest.permission.READ_MEDIA_AUDIO
    } else {
        Manifest.permission.READ_EXTERNAL_STORAGE
    }
    return ContextCompat.checkSelfPermission(context, permission) == android.content.pm.PackageManager.PERMISSION_GRANTED
}

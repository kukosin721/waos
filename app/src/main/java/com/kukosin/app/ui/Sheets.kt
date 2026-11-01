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
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun AudioSignalPathSheet(
    track: Track?,
    output: OutputDevice,
    onRequestUsb: () -> Unit,
    onDismiss: () -> Unit,
) {
    // Estado real del motor: lo que suena, no lo que pedimos.
    val engineStatus by AudioEngineStatus.status.collectAsState()
    val engineInfo = engineStatus.info
    LaunchedEffect(engineInfo.opened) {
        while (true) {
            AudioEngineStatus.refresh()
            delay(500)
        }
    }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = Ink,
        contentColor = Cream,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(horizontal = 24.dp, vertical = 8.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = Icons.Rounded.GraphicEq,
                    contentDescription = null,
                    tint = Gold,
                    modifier = Modifier.size(28.dp),
                )
                Spacer(Modifier.width(10.dp))
                Column {
                    Text(
                        "Cadena de Señal Audio (Signal Path)",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = Gold,
                    )
                    Text(
                        "Tráfico de señal punto a punto desde el archivo hasta el DAC / Salida",
                        style = MaterialTheme.typography.bodySmall,
                        color = CreamMuted,
                    )
                }
            }
            if (output.isUsb && !output.hasUsbPermission) {
                TextButton(onClick = onRequestUsb) {
                    Text("Conceder permiso USB Host", color = Gold, fontWeight = FontWeight.Bold)
                }
            }
            Spacer(Modifier.height(16.dp))

            val sampleRateStr = if (track != null && track.sampleRate > 0) {
                if (track.sampleRate >= 1000) "${track.sampleRate / 1000.0} kHz (${track.sampleRate} Hz)" else "${track.sampleRate} Hz"
            } else "44.1 kHz"

            val bitDepthStr = if (track != null && track.bitDepth > 0) "${track.bitDepth}-bit" else "16-bit"
            val channelsStr = if (track != null && track.channels > 0) {
                when (track.channels) {
                    1 -> "Mono (1.0)"
                    2 -> "Estéreo (2.0)"
                    else -> "${track.channels} canales"
                }
            } else "Estéreo (2.0)"

            val isStream = track != null && !track.isLocal
            val codecStr = track?.codecLabel ?: "FLAC / Audio"
            val bitrateStr = when {
                isStream -> "Variable (Opus/AAC)"
                track != null && track.bitrate > 0 -> "${track.bitrate / 1000} kbps"
                else -> "Variable / Lossless"
            }
            val decoderName = when {
                isStream -> "ExoPlayer · stream progresivo (YouTube Music)"
                codecStr in listOf("FLAC", "DSD", "APE", "WavPack", "ALAC", "TrueHD", "DTS", "Opus") ->
                    "NextLib Native FFmpeg Decoder"
                else -> "ExoPlayer Media3 Native Audio Decoder"
            }

            LazyColumn(
                modifier = Modifier.height(460.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                item {
                    SignalNodeCard(
                        stepNumber = "1",
                        title = "Archivo de Origen (Source)",
                        subtitle = "${track?.title ?: "Audio Local"} · $codecStr",
                        icon = Icons.Rounded.MusicNote,
                        details = listOf(
                            "Formato / Codec" to codecStr,
                            "Frecuencia Muestreo" to sampleRateStr,
                            "Profundidad Bits" to bitDepthStr,
                            "Canales" to channelsStr,
                            "Tasa de Bits" to bitrateStr,
                            "Ruta Archivo" to (track?.filePath?.ifBlank { track.fileName } ?: "Local Audio"),
                        ),
                    )
                }
                item { SignalConnectorArrow("Decodificación en Software") }
                item {
                    SignalNodeCard(
                        stepNumber = "2",
                        title = "Decodificador de Audio (Decoder)",
                        subtitle = decoderName,
                        icon = Icons.Rounded.GraphicEq,
                        details = listOf(
                            "Motor Decodificador" to decoderName,
                            "Entrada Stream" to "$codecStr ($bitDepthStr / $sampleRateStr)",
                            "Salida PCM" to if (engineInfo.opened) {
                                "${engineInfo.source.label} @ ${engineInfo.sourceSampleRate} Hz"
                            } else {
                                "Según el decodificador (sin motor abierto)"
                            },
                            "Profundidad real" to when {
                                !engineInfo.opened -> "—"
                                track != null && track.bitDepth > engineInfo.source.bits ->
                                    "${engineInfo.source.bits} bits · el archivo tiene ${track.bitDepth}: " +
                                        "el decoder está recortando (probá Decodificador = Automático o MediaCodec)"
                                else -> "${engineInfo.source.bits} bits · sin recorte respecto al archivo"
                            },
                            "Estado Motor" to if (engineInfo.passthrough) {
                                "Sin pérdidas · copia directa al DAC"
                            } else {
                                "Con conversión en el motor"
                            },
                        ),
                    )
                }
                item { SignalConnectorArrow("Procesamiento DSP & Resampler") }
                item {
                    SignalNodeCard(
                        stepNumber = "3",
                        title = "Re-Muestreo & DSP",
                        subtitle = if (engineInfo.converted) "Con conversión (el DAC no acepta el rate)" else "Sin conversión",
                        icon = Icons.Rounded.Settings,
                        details = listOf(
                            "Resampler" to if (engineInfo.converted) {
                                "Sí · Kaiser polifásico (el DAC no acepta ${engineInfo.sourceSampleRate} Hz)"
                            } else {
                                "Ninguno · el stream va al rate del archivo"
                            },
                            "Procesadores DSP" to "Ninguno (la señal no se procesa)",
                            "Velocidad / Pitch" to "Desactivado (1.0x)",
                        ),
                    )
                }
                item { SignalConnectorArrow("Transferencia AudioTrack / System Sink") }
                item {
                    SignalNodeCard(
                        stepNumber = "4",
                        title = "Motor Oboe (C++ / JNI)",
                        subtitle = if (engineStatus.mode == OutputMode.DISABLED) {
                            "No se usa · salida por el mezclador"
                        } else if (engineInfo.opened) {
                            "${engineInfo.apiLabel} · ${engineInfo.streamLabel}"
                        } else {
                            "Sin stream abierto"
                        },
                        icon = Icons.Rounded.Memory,
                        details = listOf(
                            "API de audio" to engineInfo.apiLabel,
                            "Formato del stream" to engineInfo.streamLabel,
                            "Modo de uso" to if (engineInfo.exclusive) "Exclusivo (MMAP)" else "Compartido",
                            "Buffer" to if (engineInfo.opened) {
                                "${engineInfo.bufferFrames} frames · ${Math.round(engineInfo.bufferMs)} ms"
                            } else "—",
                            "XRuns" to if (engineInfo.xRuns < 0) "No reportado" else engineInfo.xRuns.toString(),
                            "Copia de muestras" to if (engineInfo.passthrough) "memcpy (sin conversión)" else "con conversión",
                        ),
                    )
                }
                item { SignalConnectorArrow("Dispositivo de Salida Final (Hardware)") }
                item {
                    SignalNodeCard(
                        stepNumber = "5",
                        title = "Dispositivo de Salida (Output Hardware)",
                        subtitle = "${output.typeLabel} (${output.name})",
                        icon = if (output.isDac) Icons.Rounded.Usb else Icons.Rounded.Headphones,
                        isHighlight = engineStatus.bitPerfectNow,
                        details = listOf(
                            "Dispositivo Salida" to engineStatus.deviceName.ifBlank { output.name },
                            "Tipo Conexión" to output.typeLabel,
                            "Frecuencias DAC" to output.sampleRatesLabel,
                            "Modo de salida" to engineStatus.mode.label,
                            "Volumen" to when {
                                engineStatus.mode == OutputMode.DISABLED -> "Lo controla el sistema"
                                engineStatus.mode.locksVolume -> engineStatus.mode.volumeHint
                                engineStatus.mode.usesHardwareVolume -> engineStatus.mode.volumeHint
                                else -> "${engineStatus.mode.volumeHint} · ${engineInfo.volumePercent} %"
                            },
                            "Estado Bit-Perfect" to if (engineStatus.bitPerfectNow) {
                                "ACTIVO · transferencia bit a bit"
                            } else {
                                when {
                                    engineStatus.mode == OutputMode.DISABLED ->
                                        "No · modo Desactivado (mezclador Android)"
                                    engineStatus.builtinSpeaker ->
                                        "No · el altavoz interno no puede ser bit-perfect"
                                    engineStatus.eqActive -> "No · ecualizador activo (DSP de 64 bits)"
                                    !engineInfo.opened -> "Sin stream"
                                    !engineInfo.exclusive -> "Compartido con el mezclador"
                                    engineInfo.converted -> "Convertido al formato del DAC"
                                    engineInfo.volumePercent < 100 -> "Atenuado digitalmente (${engineInfo.volumePercent} %)"
                                    else -> "Inactivo"
                                }
                            },
                            "Permiso USB Host" to if (output.hasUsbPermission) "Concedido (Acceso Directo Hardware)" else "No Requerido / Estándar",
                        ),
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}

@Composable
internal fun SignalNodeCard(
    stepNumber: String,
    title: String,
    subtitle: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    details: List<Pair<String, String>>,
    isHighlight: Boolean = false,
) {
    Surface(
        color = if (isHighlight) Gold.copy(alpha = 0.12f) else InkSoft,
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (isHighlight) Gold else InkElevated,
        ),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(28.dp)
                        .clip(CircleShape)
                        .background(if (isHighlight) Gold else InkElevated),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        stepNumber,
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Bold,
                        color = if (isHighlight) Ink else Gold,
                    )
                }
                Spacer(Modifier.width(10.dp))
                Icon(icon, contentDescription = null, tint = Gold, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(8.dp))
                Column(Modifier.weight(1f)) {
                    Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold, color = Cream)
                    Text(subtitle, style = MaterialTheme.typography.bodySmall, color = if (isHighlight) Gold else CreamMuted)
                }
            }
            Spacer(Modifier.height(10.dp))
            details.forEach { (label, value) ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 1.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(label, style = MaterialTheme.typography.labelSmall, color = CreamMuted)
                    Text(value, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Medium, color = Cream)
                }
            }
        }
    }
}

@Composable
internal fun SignalConnectorArrow(label: String) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            modifier = Modifier
                .width(2.dp)
                .height(12.dp)
                .background(Gold.copy(alpha = 0.6f)),
        )
        Text(
            text = "↓ $label",
            style = MaterialTheme.typography.labelSmall,
            color = Gold,
            fontWeight = FontWeight.Bold,
        )
        Box(
            modifier = Modifier
                .width(2.dp)
                .height(12.dp)
                .background(Gold.copy(alpha = 0.6f)),
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun AlbumDetailSheet(
    album: AlbumGroup,
    currentUri: android.net.Uri?,
    isPlaying: Boolean,
    controller: MediaController? = null,
    onTrackClick: (Int) -> Unit,
    onInspectClick: (Track) -> Unit,
    onMessage: ((String) -> Unit)? = null,
    onDismiss: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = Ink,
        contentColor = Cream,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(horizontal = 20.dp, vertical = 8.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(80.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(InkSoft),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(Icons.Rounded.Album, contentDescription = null, tint = CreamMuted, modifier = Modifier.size(36.dp))
                    val art = album.artworkUri
                    if (art != null) {
                        AsyncImage(model = art, contentDescription = album.name, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
                    }
                }
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(album.name, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = Cream)
                    Text(album.artist, style = MaterialTheme.typography.bodyLarge, color = CreamMuted)
                    Text(
                        "${album.tracks.size} canciones${if (album.year > 0) " · ${album.year}" else ""}",
                        style = MaterialTheme.typography.labelMedium,
                        color = Gold,
                    )
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = {
                        playTrackList(controller, album.tracks, 0)
                        onMessage?.invoke("Reproduciendo álbum: ${album.name}")
                    },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Gold, contentColor = Ink),
                    shape = RoundedCornerShape(12.dp),
                ) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(4.dp))
                    Text("Reproducir", fontWeight = FontWeight.Bold)
                }

                Surface(
                    color = InkSoft,
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, Gold.copy(alpha = 0.5f)),
                    modifier = Modifier.clickable {
                        val msg = QueueActions.addListToQueue(controller, album.tracks, playNext = false)
                        onMessage?.invoke(msg)
                    },
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Icon(Icons.AutoMirrored.Rounded.QueueMusic, contentDescription = null, tint = Gold, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("+ Cola", style = MaterialTheme.typography.labelMedium, color = Gold, fontWeight = FontWeight.Bold)
                    }
                }
            }
            Spacer(Modifier.height(16.dp))
            LazyColumn(
                modifier = Modifier.height(340.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                itemsIndexed(album.tracks) { index, track ->
                    TrackRow(
                        track = track,
                        selected = track.uri == currentUri,
                        isPlaying = isPlaying && track.uri == currentUri,
                        controller = controller,
                        onClick = { onTrackClick(index) },
                        onInspect = { onInspectClick(track) },
                        onMessage = onMessage,
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ArtistDetailSheet(
    artist: ArtistGroup,
    currentUri: android.net.Uri?,
    isPlaying: Boolean,
    controller: MediaController? = null,
    onTrackClick: (Int) -> Unit,
    onInspectClick: (Track) -> Unit,
    onMessage: ((String) -> Unit)? = null,
    onDismiss: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = sheetState,
        containerColor = Ink,
        contentColor = Cream,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .padding(horizontal = 20.dp, vertical = 8.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(64.dp)
                        .clip(CircleShape)
                        .background(InkSoft),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(Icons.Rounded.Person, contentDescription = null, tint = Gold, modifier = Modifier.size(32.dp))
                }
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(artist.name, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = Cream)
                    Text("${artist.albumCount} álbumes · ${artist.trackCount} canciones", style = MaterialTheme.typography.bodyMedium, color = CreamMuted)
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = {
                        playTrackList(controller, artist.tracks, 0)
                        onMessage?.invoke("Reproduciendo artista: ${artist.name}")
                    },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = Gold, contentColor = Ink),
                    shape = RoundedCornerShape(12.dp),
                ) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(4.dp))
                    Text("Reproducir", fontWeight = FontWeight.Bold)
                }

                Surface(
                    color = InkSoft,
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, Gold.copy(alpha = 0.5f)),
                    modifier = Modifier.clickable {
                        val msg = QueueActions.addListToQueue(controller, artist.tracks, playNext = false)
                        onMessage?.invoke(msg)
                    },
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Icon(Icons.AutoMirrored.Rounded.QueueMusic, contentDescription = null, tint = Gold, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("+ Cola", style = MaterialTheme.typography.labelMedium, color = Gold, fontWeight = FontWeight.Bold)
                    }
                }
            }
            Spacer(Modifier.height(16.dp))
            LazyColumn(
                modifier = Modifier.height(340.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                itemsIndexed(artist.tracks) { index, track ->
                    TrackRow(
                        track = track,
                        selected = track.uri == currentUri,
                        isPlaying = isPlaying && track.uri == currentUri,
                        controller = controller,
                        onClick = { onTrackClick(index) },
                        onInspect = { onInspectClick(track) },
                        onMessage = onMessage,
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}

@Composable
internal fun MetadataInspectorDialog(
    track: Track,
    output: OutputDevice,
    analysis: com.kukosin.app.data.analysis.AudioAnalysis?,
    onDismiss: () -> Unit,
    onOpenSignalPath: () -> Unit,
    onAnalyzeClick: () -> Unit,
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .fillMaxHeight(0.82f)
                .clip(RoundedCornerShape(24.dp)),
            color = Ink,
            tonalElevation = 8.dp,
            border = androidx.compose.foundation.BorderStroke(1.dp, Gold.copy(alpha = 0.4f)),
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(20.dp),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Rounded.Info, contentDescription = null, tint = Gold, modifier = Modifier.size(24.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Info y Autenticidad",
                            style = MaterialTheme.typography.titleLarge,
                            color = Gold,
                            fontWeight = FontWeight.Bold,
                        )
                    }
                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Rounded.Close, contentDescription = "Cerrar", tint = CreamMuted)
                    }
                }

                Spacer(Modifier.height(8.dp))

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    item {
                        AnalysisPanel(
                            analysis = analysis?.takeIf { it.uri == track.uri },
                            scrollable = false,
                            onAnalyzeClick = onAnalyzeClick,
                            canAnalyze = track.isLocal,
                        )
                    }
                    item {
                        Spacer(Modifier.height(4.dp))
                        MetaGroupHeader("Etiquetas del Archivo")
                        MetaItem("Título", track.title)
                        MetaItem("Artista", track.effectiveArtist)
                        MetaItem("Álbum", track.album)
                        if (track.albumArtist.isNotBlank()) MetaItem("Artista del Álbum", track.albumArtist)
                        if (track.genre.isNotBlank()) MetaItem("Género", track.genre)
                        if (track.composer.isNotBlank()) MetaItem("Compositor", track.composer)
                        if (track.trackNumber > 0) MetaItem("Número de Pista", track.trackNumber.toString())
                        if (track.discNumber > 0) MetaItem("Número de Disco", track.discNumber.toString())
                        if (track.year > 0) MetaItem("Año / Fecha", track.year.toString())
                        if (track.comment.isNotBlank()) MetaItem("Comentario", track.comment)
                    }
                    item {
                        Spacer(Modifier.height(4.dp))
                        MetaGroupHeader("Especificaciones de Audio")
                        MetaItem("Codec / Formato", track.codecLabel)
                        MetaItem("Tipo Mime", track.mimeType)
                        MetaItem("Frecuencia de Muestreo", if (track.sampleRate > 0) "${track.sampleRate} Hz" else "N/A")
                        MetaItem("Profundidad de Bits", if (track.bitDepth > 0) "${track.bitDepth}-bit" else "N/A")
                        MetaItem("Canales", if (track.channels > 0) "${track.channels}" else "Estéreo")
                        MetaItem("Tasa de Bits", if (track.bitrate > 0) "${track.bitrate / 1000} kbps" else "Variable / Lossless")
                        MetaItem("Duración", formatTime(track.durationMs))
                    }
                    item {
                        Spacer(Modifier.height(4.dp))
                        MetaGroupHeader("Archivo e Integración Bit-Perfect")
                        MetaItem("Ruta de Archivo", track.filePath.ifBlank { track.uri.toString() })
                        MetaItem("Tamaño de Archivo", track.fileSizeLabel)
                        MetaItem("Salida Activa", "${output.typeLabel} (${output.name})", onClick = onOpenSignalPath)
                        MetaItem(
                            "Estado Bit-Perfect",
                            if (AudioEngineStatus.status.value.bitPerfectNow) "Activo · bit a bit" else "No activo",
                            onClick = onOpenSignalPath,
                        )
                        MetaItem("Frecuencias Soportadas DAC", output.sampleRatesLabel, onClick = onOpenSignalPath)
                    }
                    item {
                        Spacer(Modifier.height(12.dp))
                    }
                }
            }
        }
    }
}

@Composable
internal fun MetaGroupHeader(title: String) {
    Text(
        text = title,
        style = MaterialTheme.typography.labelLarge,
        color = Gold,
        fontWeight = FontWeight.Bold,
        modifier = Modifier.padding(vertical = 4.dp),
    )
}

@Composable
internal fun MetaItem(label: String, value: String, onClick: (() -> Unit)? = null) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(vertical = 2.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium, color = CreamMuted, modifier = Modifier.weight(0.4f))
        Text(value, style = MaterialTheme.typography.bodyMedium, color = if (onClick != null) Gold else Cream, fontWeight = FontWeight.Medium, modifier = Modifier.weight(0.6f))
    }
}

package com.kukosin.app.data

import android.content.Context
import android.net.Uri
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Persistencia de la caché de sondeo de la biblioteca.
 *
 * Guarda en `filesDir` lo que costó leer de cada archivo (duración, sample rate,
 * profundidad de bits, carátula, etiquetas). Sin Room ni dependencias nuevas: un
 * JSON escrito de forma atómica (tmp + rename), que es suficiente para un mapa
 * plano de "uri → pista enriquecida".
 *
 * Se invalida al pedir "volver a escanear".
 */
object LibraryCacheStore {

    private const val FILE_NAME = "library_probe_cache.json"
    private const val VERSION = 1

    /** Tope defensivo: si alguien tiene 100k archivos, no queremos 40 MB de JSON. */
    private const val MAX_ENTRIES = 60_000

    fun load(context: Context): Map<String, Track> {
        val file = File(context.filesDir, FILE_NAME)
        if (!file.exists()) return emptyMap()
        return runCatching<Map<String, Track>> {
            val root = JSONObject(file.readText())
            if (root.optInt("version", 0) != VERSION) {
                emptyMap()
            } else {
                val array = root.optJSONArray("tracks") ?: JSONArray()
                val result = HashMap<String, Track>(array.length())
                for (i in 0 until array.length()) {
                    val item = array.optJSONObject(i) ?: continue
                    val track = fromJson(item) ?: continue
                    result[track.uri.toString()] = track
                }
                result
            }
        }.getOrElse { emptyMap() }
    }

    fun save(context: Context, entries: Map<String, Track>) {
        if (entries.isEmpty()) return
        runCatching {
            val array = JSONArray()
            var count = 0
            for (track in entries.values) {
                if (count >= MAX_ENTRIES) break
                array.put(toJson(track))
                count++
            }
            val root = JSONObject()
                .put("version", VERSION)
                .put("tracks", array)

            val file = File(context.filesDir, FILE_NAME)
            val tmp = File(context.filesDir, "$FILE_NAME.tmp")
            tmp.writeText(root.toString())
            if (file.exists()) file.delete()
            tmp.renameTo(file)
        }
    }

    fun clear(context: Context) {
        runCatching {
            File(context.filesDir, FILE_NAME).delete()
            File(context.filesDir, "$FILE_NAME.tmp").delete()
        }
    }

    // ------------------------------------------------------------------ JSON

    private fun toJson(track: Track): JSONObject = JSONObject()
        .put("id", track.id)
        .put("uri", track.uri.toString())
        .put("title", track.title)
        .put("artist", track.artist)
        .put("album", track.album)
        .put("albumArtist", track.albumArtist)
        .put("durationMs", track.durationMs)
        .put("albumId", track.albumId)
        .put("bitrate", track.bitrate)
        .put("sampleRate", track.sampleRate)
        .put("bitDepth", track.bitDepth)
        .put("channels", track.channels)
        .put("mimeType", track.mimeType)
        .put("fileName", track.fileName)
        .put("year", track.year)
        .put("trackNumber", track.trackNumber)
        .put("discNumber", track.discNumber)
        .put("genre", track.genre)
        .put("composer", track.composer)
        .put("filePath", track.filePath)
        .put("fileSize", track.fileSize)
        .put("comment", track.comment)

    private fun fromJson(json: JSONObject): Track? {
        val uriString = json.optString("uri", "")
        if (uriString.isBlank()) return null
        return Track(
            id = json.optLong("id", 0L),
            uri = Uri.parse(uriString),
            title = json.optString("title", "Untitled"),
            artist = json.optString("artist", "Unknown artist"),
            album = json.optString("album", "Unknown album"),
            albumArtist = json.optString("albumArtist", ""),
            durationMs = json.optLong("durationMs", 0L),
            albumId = json.optLong("albumId", 0L),
            bitrate = json.optInt("bitrate", 0),
            sampleRate = json.optInt("sampleRate", 0),
            bitDepth = json.optInt("bitDepth", 0),
            channels = json.optInt("channels", 0),
            mimeType = json.optString("mimeType", ""),
            fileName = json.optString("fileName", ""),
            year = json.optInt("year", 0),
            trackNumber = json.optInt("trackNumber", 0),
            discNumber = json.optInt("discNumber", 0),
            genre = json.optString("genre", ""),
            composer = json.optString("composer", ""),
            filePath = json.optString("filePath", ""),
            fileSize = json.optLong("fileSize", 0L),
            comment = json.optString("comment", ""),
        )
    }
}

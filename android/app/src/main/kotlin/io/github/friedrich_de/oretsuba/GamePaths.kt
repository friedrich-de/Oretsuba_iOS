package io.github.friedrich_de.oretsuba

import android.content.Context
import java.io.File

/**
 * Where the port finds the player's game files and keeps its own.
 *
 * Everything lives in the app's own folder on the shared storage,
 * Android/data/<this app>/files, which a computer connected with a cable can
 * open: the game folder (the disc's SET_UP folder, or the whole disc), a
 * mining.txt next to it, and the saves in Saves/jpn.
 */
internal class GamePaths(context: Context) {
    private val preferences = context.getSharedPreferences("launcher", Context.MODE_PRIVATE)

    /** The app's folder. The device's own storage when no shared storage is mounted. */
    val root: File = context.getExternalFilesDir(null) ?: context.filesDir

    /** Saves and settings, under the name the Apple port gives the Japanese original's saves. */
    val saves: File
        get() = File(root, "Saves/jpn").apply { mkdirs() }

    /** The mining.txt the launcher's button puts next to the game folder (docs/MINING.md). */
    val miningFile: File
        get() = File(root, MINING_FILE)

    /** Keeping the reader's place (docs/AUTOSAVE.md): the launcher's switch and the save it leads to. */
    var keepsPlace: Boolean
        get() = preferences.getBoolean(KEEPS_PLACE, false)
        set(value) = preferences.edit().putBoolean(KEEPS_PLACE, value).apply()

    val hasAutosave: Boolean
        get() = File(saves, "DATA1200.SAV").isFile

    /** The folder holding SCRIPT.LPK, searched a few levels deep. */
    fun findGameFolder(): File? = search(root, 3)

    private fun search(dir: File, depth: Int): File? {
        val items = dir.listFiles() ?: return null
        if (items.any { it.isFile && it.name.equals(SCRIPT_ARCHIVE, ignoreCase = true) }) return dir
        if (depth == 0) return null
        for (item in items.sortedBy { it.name }) {
            if (item.isDirectory && !item.name.startsWith(".")) search(item, depth - 1)?.let { return it }
        }
        return null
    }

    companion object {
        const val AUTOSAVE_LINES = 5
        const val MINING_FILE = "mining.txt"
        private const val SCRIPT_ARCHIVE = "SCRIPT.LPK"
        private const val KEEPS_PLACE = "keepsPlace"
    }
}

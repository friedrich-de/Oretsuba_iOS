// Hands a card's picture and sound to AnkiDroid (docs/MINING.md). AnkiDroid
// takes a file as a content:// address it may read; this provider is that
// address for the files in the app's cache folder "anki-media", and for
// nothing else. It is not exported: only an app that was granted one address
// (AnkiDroid, for the moment a card is added) can open it.
package io.github.friedrich_de.oretsuba

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File
import java.io.FileNotFoundException

class AnkiMediaProvider : ContentProvider() {
    companion object {
        fun authority(context: Context): String = context.packageName + ".ankimedia"

        /** The folder the files wait in. The mining kit writes them, [AnkiDroid] removes them. */
        fun folder(context: Context): File = File(context.cacheDir, "anki-media").apply { mkdirs() }

        fun uriFor(context: Context, name: String): Uri =
            Uri.Builder().scheme("content").authority(authority(context)).appendPath(name).build()

        /**
         * The type AnkiDroid is told. It names the stored file's ending after
         * this type, so every one here is a type Android knows an ending for.
         * For Ogg sound that ending is .oga, whatever the file was called.
         */
        fun typeOf(name: String): String = when (name.substringAfterLast('.', "").lowercase()) {
            "jpg", "jpeg" -> "image/jpeg"
            "png" -> "image/png"
            "webp" -> "image/webp"
            "gif" -> "image/gif"
            "ogg", "oga", "opus" -> "audio/ogg"
            "mp3" -> "audio/mpeg"
            "m4a" -> "audio/mp4"
            "aac" -> "audio/aac"
            "wav" -> "audio/x-wav"
            "flac" -> "audio/flac"
            else -> "application/octet-stream"
        }

        /** The file behind a name, or null when the name points anywhere but into the folder. */
        fun fileFor(context: Context, name: String): File? {
            if (name.isEmpty() || name.startsWith(".") || name.contains('/') || name.contains('\\')) return null
            val folder = folder(context).canonicalFile
            val file = File(folder, name).canonicalFile
            return if (file.parentFile == folder && file.isFile) file else null
        }
    }

    /** Android makes a provider before anything else of the app: AnkiDroid.kt gets the app here. */
    override fun onCreate(): Boolean {
        context?.let { AnkiDroid.attach(it) }
        return true
    }

    private fun fileOf(uri: Uri): File? {
        val context = context ?: return null
        val segments = uri.pathSegments
        return if (segments.size == 1) fileFor(context, segments[0]) else null
    }

    override fun getType(uri: Uri): String? = fileOf(uri)?.let { typeOf(it.name) }

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw SecurityException("These files are read only.")
        val file = fileOf(uri) ?: throw FileNotFoundException(uri.toString())
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    /** Name and size, which some readers ask for before they open a file. */
    override fun query(uri: Uri, projection: Array<String>?, selection: String?, selectionArgs: Array<String>?,
                       sortOrder: String?): Cursor? {
        val file = fileOf(uri) ?: return null
        val columns = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        val cursor = MatrixCursor(columns, 1)
        cursor.addRow(columns.map { column ->
            when (column) {
                OpenableColumns.DISPLAY_NAME -> file.name
                OpenableColumns.SIZE -> file.length()
                else -> null
            }
        })
        return cursor
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<String>?): Int = 0
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<String>?): Int = 0
}

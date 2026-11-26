package io.github.friedrich_de.oretsuba

import android.content.Context
import android.net.Uri
import android.os.storage.StorageManager
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.io.File
import java.io.IOException
import java.util.UUID

/**
 * Copies a folder the player picked with the system's folder picker into the
 * app's folder. The copy appears under its name only once it is complete; a
 * folder of that name that was there before is replaced then, not earlier.
 */
internal object FolderImport {
    /** A failure the player can act on; the message is shown as it is. */
    class Failure(message: String) : IOException(message)

    private class Item(val id: String, val path: String, val size: Long)

    /** The picked folder's name, as it will be called in the app's folder. */
    fun name(context: Context, tree: Uri): String {
        val folder = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree))
        val name = context.contentResolver.query(folder, arrayOf(Document.COLUMN_DISPLAY_NAME), null, null, null)
            ?.use { if (it.moveToFirst()) it.getString(0) else null }
        return safeName(name) ?: "Game"
    }

    /**
     * Copies [tree] into [into]. [progress] gets the bytes copied and the
     * bytes in all, a few times a second, on the calling thread.
     */
    fun copy(context: Context, tree: Uri, into: File, progress: (done: Long, total: Long) -> Unit) {
        val name = name(context, tree)
        val items = ArrayList<Item>()
        list(context, tree, DocumentsContract.getTreeDocumentId(tree), "", items, 0)
        if (items.isEmpty()) throw Failure("The chosen folder is empty.")
        val total = items.sumOf { it.size }
        // Leave some room beyond the files.
        val free = freeSpace(context, into)
        if (free < total + (64L shl 20)) {
            throw Failure("Not enough free space: the folder needs ${megabytes(total)}, ${megabytes(free)} are free.")
        }
        val target = File(into, name)
        val staging = File(into, ".import-${UUID.randomUUID()}")
        try {
            var done = 0L
            var told = 0L
            val buffer = ByteArray(1 shl 20)
            for (item in items) {
                val file = File(staging, item.path)
                file.parentFile?.mkdirs()
                val source = DocumentsContract.buildDocumentUriUsingTree(tree, item.id)
                val input = context.contentResolver.openInputStream(source)
                    ?: throw Failure("${item.path} could not be opened.")
                input.use { stream ->
                    file.outputStream().use { out ->
                        while (true) {
                            val n = stream.read(buffer)
                            if (n < 0) break
                            out.write(buffer, 0, n)
                            done += n
                            val now = System.nanoTime()
                            if (now - told > 200_000_000L) {
                                told = now
                                progress(done, total)
                            }
                        }
                    }
                }
            }
            progress(done, total)
            // Out with the old copy only now that the new one is whole.
            val old = File(into, ".old-${UUID.randomUUID()}")
            val replaced = target.exists()
            if (replaced && !target.renameTo(old)) throw Failure("The folder $name in the app could not be replaced.")
            if (!staging.renameTo(target)) {
                if (replaced) old.renameTo(target)
                throw Failure("The copy could not be put in its place.")
            }
            if (replaced) old.deleteRecursively()
        } finally {
            if (staging.exists()) staging.deleteRecursively()
        }
    }

    /** Leftovers of a copy the system cut short (the app was closed while copying). */
    fun removeLeftovers(root: File) {
        root.listFiles { file -> file.isDirectory && (file.name.startsWith(".import-") || file.name.startsWith(".old-")) }
            ?.forEach { it.deleteRecursively() }
    }

    private fun list(context: Context, tree: Uri, parent: String, prefix: String, out: MutableList<Item>, depth: Int) {
        if (depth > 16) return
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent)
        val columns = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                              Document.COLUMN_SIZE)
        val folders = ArrayList<Pair<String, String>>()
        context.contentResolver.query(children, columns, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                val name = safeName(rows.getString(1)) ?: continue
                if (rows.getString(2) == Document.MIME_TYPE_DIR) {
                    folders += rows.getString(0) to "$prefix$name/"
                } else {
                    out += Item(rows.getString(0), prefix + name, if (rows.isNull(3)) 0L else rows.getLong(3))
                }
            }
        } ?: throw Failure("The chosen folder could not be read.")
        for ((id, path) in folders) list(context, tree, id, path, out, depth + 1)
    }

    /** What can be written to [folder], counting what the system would clear of its caches to make room. */
    private fun freeSpace(context: Context, folder: File): Long = try {
        val storage = context.getSystemService(StorageManager::class.java)
        storage.getAllocatableBytes(storage.getUuidForPath(folder))
    } catch (e: Exception) {
        @Suppress("UsableSpace")
        folder.usableSpace
    }

    /** A name that stays inside the folder it is put into, or null. */
    private fun safeName(name: String?): String? {
        val clean = name?.replace('/', '_')?.replace('\u0000', '_')?.trim() ?: return null
        return clean.takeUnless { it.isEmpty() || it == "." || it == ".." }
    }

    fun megabytes(bytes: Long): String =
        if (bytes >= 1L shl 30) "%.2f GB".format(bytes / (1L shl 30).toDouble()) else "${bytes shr 20} MB"
}

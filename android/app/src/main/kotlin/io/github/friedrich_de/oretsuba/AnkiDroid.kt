// Cards for AnkiDroid on the same device (docs/MINING.md). AnkiDroid lets
// other apps read its decks and note types and add notes through a content
// provider, once the player has allowed it. This file speaks to that provider
// directly, by the addresses and columns AnkiDroid documents for it
// (FlashCardsContract), and answers the mining kit in JSON:
//
//   status  {}                                   -> {installed, allowed, mediaFolder}
//   info    {}                                   -> {decks: [name], noteTypes: [{name, fields: [name]}]}
//   known   {noteType, field, value}             -> {known}
//   add     {deck, noteType, fields: {name: html}, tags: [tag], allowDuplicate,
//            media: [{name, sound, fields: [name]}]}
//                                                -> {id, missing: [name]}
//
// The files of `media` wait in `mediaFolder` under their names; AnkiDroid
// copies them into its own media folder and the reference to each (its
// stored name) is put at the end of the fields it belongs to. Anything that
// goes wrong comes back as {error: a sentence for the player, code}; the
// codes are missing (AnkiDroid is not there), permission, noteType, deck,
// empty (the first field), duplicate and failed.
package io.github.friedrich_de.oretsuba

import android.app.Activity
import android.content.ContentResolver
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import org.json.JSONArray
import org.json.JSONObject

object AnkiDroid {
    /** The permission AnkiDroid defines for its cards; the player grants it once. */
    const val PERMISSION = "com.ichi2.anki.permission.READ_WRITE_DATABASE"

    private const val AUTHORITY = "com.ichi2.anki.flashcards"
    private val base: Uri = Uri.parse("content://$AUTHORITY")
    private val notes: Uri = Uri.withAppendedPath(base, "notes")
    private val models: Uri = Uri.withAppendedPath(base, "models")
    private val decks: Uri = Uri.withAppendedPath(base, "decks")
    private val media: Uri = Uri.withAppendedPath(base, "media")

    /** Anki keeps a note's fields in one text, parted by this character. */
    private const val FIELD_SEPARATOR = "\u001f"

    private const val NOT_THERE = "AnkiDroid is not on this device."
    private const val NOT_ALLOWED = "AnkiDroid has not allowed this app to add cards yet. Allow it in the launcher."
    private const val NO_ANSWER = "AnkiDroid did not answer. Open AnkiDroid once, then try again."

    /** The app that answers for AnkiDroid's cards, or null when there is none. */
    fun packageName(context: Context): String? =
        context.packageManager.resolveContentProvider(AUTHORITY, 0)?.packageName

    fun installed(context: Context): Boolean = packageName(context) != null

    fun allowed(context: Context): Boolean =
        context.checkSelfPermission(PERMISSION) == PackageManager.PERMISSION_GRANTED

    /** Shows the system's question; the answer arrives in the activity's onRequestPermissionsResult. */
    fun ask(activity: Activity, requestCode: Int) = activity.requestPermissions(arrayOf(PERMISSION), requestCode)

    /** One request of the mining kit. Never throws; may take a moment, so not on the main thread. */
    fun call(context: Context, method: String, json: String): String {
        val app = context.applicationContext
        val answer: JSONObject = try {
            when (method) {
                "status" -> status(app)
                "info" -> guarded(app) { info(app.contentResolver) }
                "known" -> guarded(app) { known(app.contentResolver, JSONObject(json)) }
                "add" -> guarded(app) { add(app, JSONObject(json)) }
                else -> problem("AnkiDroid cannot do \"$method\".", "failed")
            }
        } catch (e: SecurityException) {
            problem(NOT_ALLOWED, "permission")
        } catch (e: Throwable) {
            problem(NO_ANSWER + " (" + (e.message ?: e.javaClass.simpleName) + ")", "failed")
        }
        return answer.toString()
    }

    private fun problem(text: String, code: String): JSONObject = JSONObject().put("error", text).put("code", code)

    private inline fun guarded(context: Context, work: () -> JSONObject): JSONObject = when {
        !installed(context) -> problem(NOT_THERE, "missing")
        !allowed(context) -> problem(NOT_ALLOWED, "permission")
        else -> work()
    }

    private fun status(context: Context): JSONObject = JSONObject()
        .put("installed", installed(context))
        .put("allowed", installed(context) && allowed(context))
        .put("mediaFolder", AnkiMediaProvider.folder(context).absolutePath)

    // ------------------------------------------------------------ decks and note types

    private class NoteType(val id: Long, val name: String, val fields: List<String>)
    private class Deck(val id: Long, val name: String)

    private fun noteTypes(resolver: ContentResolver): List<NoteType>? =
        resolver.query(models, null, null, null, null)?.use { rows ->
            val id = rows.getColumnIndexOrThrow("_id")
            val name = rows.getColumnIndexOrThrow("name")
            val fields = rows.getColumnIndexOrThrow("field_names")
            val list = ArrayList<NoteType>()
            while (rows.moveToNext()) {
                val names = (rows.getString(fields) ?: "").split(FIELD_SEPARATOR).filter { it.isNotEmpty() }
                if (names.isNotEmpty()) list.add(NoteType(rows.getLong(id), rows.getString(name) ?: "", names))
            }
            list.sortedBy { it.name.lowercase() }
        }

    /** The decks a card can go into: all but the filtered ones. */
    private fun deckList(resolver: ContentResolver): List<Deck>? =
        resolver.query(decks, null, null, null, null)?.use { rows ->
            val id = rows.getColumnIndexOrThrow("deck_id")
            val name = rows.getColumnIndexOrThrow("deck_name")
            val filtered = rows.getColumnIndex("deck_dyn")
            val list = ArrayList<Deck>()
            while (rows.moveToNext()) {
                if (filtered >= 0 && rows.getInt(filtered) != 0) continue
                list.add(Deck(rows.getLong(id), rows.getString(name) ?: ""))
            }
            list.sortedBy { it.name.lowercase() }
        }

    private fun info(resolver: ContentResolver): JSONObject {
        val deckRows = deckList(resolver) ?: return problem(NO_ANSWER, "failed")
        val typeRows = noteTypes(resolver) ?: return problem(NO_ANSWER, "failed")
        val deckNames = JSONArray()
        for (deck in deckRows) deckNames.put(deck.name)
        val types = JSONArray()
        for (type in typeRows) types.put(JSONObject().put("name", type.name).put("fields", JSONArray(type.fields)))
        return JSONObject().put("decks", deckNames).put("noteTypes", types)
    }

    // ------------------------------------------------------------ is the word there already

    /** A text inside the quotes of an Anki search: the marks a search reads are taken as plain letters. */
    private fun searchText(text: String): String {
        val out = StringBuilder(text.length + 8)
        for (c in text) {
            if (c == '\\' || c == '"' || c == '*' || c == '_' || c == ':') out.append('\\')
            out.append(c)
        }
        return out.toString()
    }

    /** Whether a note of this type has exactly `value` in `field`: Anki's own search does the comparing. */
    private fun isKnown(resolver: ContentResolver, noteType: String, field: String, value: String): Boolean? {
        if (value.isBlank()) return false
        val search = "\"note:${searchText(noteType)}\" \"${searchText(field)}:${searchText(value)}\""
        return resolver.query(notes, arrayOf("_id"), search, null, null)?.use { it.count > 0 }
    }

    private fun known(resolver: ContentResolver, ask: JSONObject): JSONObject {
        val found = isKnown(resolver, ask.getString("noteType"), ask.getString("field"), ask.getString("value"))
            ?: return problem(NO_ANSWER, "failed")
        return JSONObject().put("known", found)
    }

    // ------------------------------------------------------------ a new card

    private fun add(context: Context, card: JSONObject): JSONObject {
        val resolver = context.contentResolver
        val typeName = card.getString("noteType")
        val deckName = card.getString("deck")
        val type = (noteTypes(resolver) ?: return problem(NO_ANSWER, "failed")).firstOrNull { it.name == typeName }
            ?: return problem("AnkiDroid has no note type \"$typeName\" any more. Fetch the lists again in the launcher.", "noteType")
        val deck = (deckList(resolver) ?: return problem(NO_ANSWER, "failed")).firstOrNull { it.name == deckName }
            ?: return problem("AnkiDroid has no deck \"$deckName\" any more. Fetch the lists again in the launcher.", "deck")

        val given = card.optJSONObject("fields") ?: JSONObject()
        val values = type.fields.map { given.optString(it, "").replace(FIELD_SEPARATOR, "") }.toMutableList()

        // Anki tells its notes apart by their first field and takes none without it.
        if (values[0].isBlank()) {
            return problem("The card's first field, \"${type.fields[0]}\", is empty.", "empty")
        }
        if (!card.optBoolean("allowDuplicate", false) && isKnown(resolver, type.name, type.fields[0], values[0]) == true) {
            return problem("This word is in AnkiDroid already.", "duplicate")
        }

        // Pictures and sound first: the note names each file as AnkiDroid stored it.
        val missing = JSONArray()
        val files = card.optJSONArray("media") ?: JSONArray()
        for (i in 0 until files.length()) {
            val file = files.getJSONObject(i)
            val name = file.getString("name")
            val stored = store(context, name)
            if (stored == null) {
                missing.put(name)
                continue
            }
            val reference = if (file.optBoolean("sound", false)) "[sound:$stored]" else "<img src=\"$stored\">"
            val into = file.optJSONArray("fields") ?: JSONArray()
            for (j in 0 until into.length()) {
                val index = type.fields.indexOf(into.getString(j))
                if (index >= 0) values[index] = values[index] + reference
            }
        }

        val tags = card.optJSONArray("tags") ?: JSONArray()
        val tagText = (0 until tags.length()).map { tags.getString(it).trim().replace(Regex("\\s+"), "_") }
            .filter { it.isNotEmpty() }.joinToString(" ")
        val row = ContentValues()
        row.put("mid", type.id)
        row.put("flds", values.joinToString(FIELD_SEPARATOR))
        if (tagText.isNotEmpty()) row.put("tags", tagText)
        val note = resolver.insert(notes, row) ?: return problem("AnkiDroid did not take the card.", "failed")

        // A new note's cards sit in the note type's own deck; move them to the chosen one.
        val cards = Uri.withAppendedPath(note, "cards")
        resolver.query(cards, null, null, null, null)?.use { rows ->
            val ord = rows.getColumnIndexOrThrow("ord")
            while (rows.moveToNext()) {
                val to = ContentValues()
                to.put("deck_id", deck.id)
                resolver.update(Uri.withAppendedPath(cards, rows.getString(ord)), to, null, null)
            }
        }
        return JSONObject().put("id", note.lastPathSegment ?: "").put("missing", missing)
    }

    /**
     * Gives AnkiDroid one waiting file. Returns the name AnkiDroid stored it
     * under, or null when it did not take it. The waiting file is removed
     * either way.
     */
    private fun store(context: Context, name: String): String? {
        val file = AnkiMediaProvider.fileFor(context, name) ?: return null
        val owner = packageName(context) ?: return null
        val address = AnkiMediaProvider.uriFor(context, name)
        context.grantUriPermission(owner, address, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        try {
            val row = ContentValues()
            row.put("file_uri", address.toString())
            row.put("preferred_name", name.substringBeforeLast('.').replace(' ', '_'))
            val stored = context.contentResolver.insert(media, row)?.path ?: return null
            return stored.substringAfterLast('/').ifEmpty { null }
        } catch (e: SecurityException) {
            throw e
        } catch (e: Exception) {
            return null
        } finally {
            context.revokeUriPermission(address, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            file.delete()
        }
    }
}

// Checks the cards for AnkiDroid (AnkiDroid.kt) against the real AnkiDroid on
// a device or an emulator. Only debug builds hold it. With AnkiDroid installed
// and opened once:
//
//   adb shell am instrument -w io.github.friedrich_de.oretsuba/.AnkiDroidCheck
//
// It prints one line for each thing it checked and ends with "passed" or
// "FAILED". It adds a handful of notes of the type "Basic" to the deck
// "Default" (another deck with -e deck NAME), each with a word made from the
// time, so run it against an AnkiDroid whose collection does not matter.
package io.github.friedrich_de.oretsuba

import android.app.Activity
import android.app.Instrumentation
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

class AnkiDroidCheck : Instrumentation() {
    private val report = StringBuilder()
    private var failed = 0
    private var deck = "Default"

    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        arguments?.getString("deck")?.takeIf { it.isNotEmpty() }?.let { deck = it }
        start()
    }

    override fun onStart() {
        try {
            run(targetContext)
        } catch (e: Throwable) {
            check(false, "the check runs to its end ($e)")
        }
        report.append(if (failed == 0) "AnkiDroid check passed\n" else "AnkiDroid check FAILED: $failed\n")
        val result = Bundle()
        result.putString(REPORT_KEY_STREAMRESULT, report.toString())
        finish(if (failed == 0) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
    }

    private fun check(ok: Boolean, what: String) {
        report.append(if (ok) "  ok    " else "  FAIL  ").append(what).append('\n')
        if (!ok) failed++
    }

    private fun note(text: String) {
        report.append("        ").append(text).append('\n')
    }

    private fun ask(context: Context, method: String, request: JSONObject = JSONObject()): JSONObject =
        JSONObject(AnkiDroid.call(context, method, request.toString()))

    private fun card(front: String, back: String = "", toDeck: String = deck, type: String = "Basic"): JSONObject =
        JSONObject().put("deck", toDeck).put("noteType", type)
            .put("fields", JSONObject().put("Front", front).put("Back", back))
            .put("tags", JSONArray().put("oretsuba-check").put("two words"))

    private fun known(context: Context, value: String): Boolean {
        val answer = ask(context, "known", JSONObject().put("noteType", "Basic").put("field", "Front").put("value", value))
        // No answer must not pass for "not known": the mining kit takes it for a failure and makes no card.
        if (!answer.has("known")) check(false, "whether \"$value\" is known is answered ($answer)")
        return answer.optBoolean("known", false)
    }

    private fun media(name: String, sound: Boolean, field: String): JSONObject =
        JSONObject().put("name", name).put("sound", sound).put("fields", JSONArray().put(field))

    private fun run(context: Context) {
        var status = ask(context, "status")
        check(status.optBoolean("installed"), "AnkiDroid is on this device")
        if (!status.optBoolean("installed")) return
        if (!status.optBoolean("allowed")) {
            // The system's question needs a screen; a check grants itself what the player would.
            note("not allowed yet: granting " + AnkiDroid.PERMISSION)
            if (Build.VERSION.SDK_INT >= 28) {
                try {
                    uiAutomation.grantRuntimePermission(context.packageName, AnkiDroid.PERMISSION)
                } catch (e: Throwable) {
                    note("granting failed: $e")
                }
            } else {
                note("grant it by hand: adb shell pm grant ${context.packageName} ${AnkiDroid.PERMISSION}")
            }
            status = ask(context, "status")
        }
        check(status.optBoolean("allowed"), "AnkiDroid lets this app at its cards")
        if (!status.optBoolean("allowed")) return
        val folder = File(status.getString("mediaFolder"))
        check(folder.isDirectory, "the folder the files wait in is there: $folder")

        // ---- decks and note types
        val info = ask(context, "info")
        val decks = info.optJSONArray("decks") ?: JSONArray()
        val types = info.optJSONArray("noteTypes") ?: JSONArray()
        note("decks: $decks")
        note("note types: " + (0 until types.length()).joinToString { types.getJSONObject(it).getString("name") })
        check((0 until decks.length()).any { decks.getString(it) == deck }, "the deck \"$deck\" is among AnkiDroid's decks")
        val basic = (0 until types.length()).map { types.getJSONObject(it) }.firstOrNull { it.getString("name") == "Basic" }
        check(basic != null && basic.getJSONArray("fields").toString() == "[\"Front\",\"Back\"]",
            "the note type \"Basic\" is there with the fields Front and Back")
        if (basic == null) return

        // ---- one card with a picture and three sounds
        val stamp = System.currentTimeMillis().toString(36)
        val word = "食べる $stamp"
        check(!known(context, word), "a new word is not known")

        val picture = Bitmap.createBitmap(64, 48, Bitmap.Config.ARGB_8888)
        picture.eraseColor(Color.rgb(40, 90, 160))
        File(folder, "oretsuba_$stamp.jpg").outputStream().use { picture.compress(Bitmap.CompressFormat.JPEG, 85, it) }
        // No real sounds: AnkiDroid stores what it is given, and the question is under which name.
        for (ending in listOf("ogg", "m4a", "mp3")) File(folder, "oretsuba_$stamp.$ending").writeBytes(ByteArray(2000) { 7 })

        val first = card(word, "to eat")
            .put("media", JSONArray()
                .put(media("oretsuba_$stamp.jpg", false, "Back"))
                .put(media("oretsuba_$stamp.ogg", true, "Back"))
                .put(media("oretsuba_$stamp.m4a", true, "Back"))
                .put(media("oretsuba_$stamp.mp3", true, "Back"))
                .put(media("never_written_$stamp.jpg", false, "Back")))
        val added = ask(context, "add", first)
        note("add answered: $added")
        val id = added.optString("id")
        check(id.isNotEmpty() && !added.has("error"), "a card is added and its number comes back")
        check(added.optJSONArray("missing")?.toString() == "[\"never_written_$stamp.jpg\"]",
            "a file that was never written is named as missing, the others are not")
        check(folder.listFiles().orEmpty().isEmpty(), "no file is left waiting")
        if (id.isEmpty()) return

        val resolver = context.contentResolver
        val notes = Uri.parse("content://com.ichi2.anki.flashcards/notes")
        var fields = ""
        var tags = ""
        resolver.query(Uri.withAppendedPath(notes, id), null, null, null, null)?.use { row ->
            if (row.moveToFirst()) {
                fields = row.getString(row.getColumnIndexOrThrow("flds")) ?: ""
                tags = row.getString(row.getColumnIndexOrThrow("tags")) ?: ""
            }
        }
        note("the note's fields: " + fields.replace('\u001f', '|'))
        note("the note's tags: $tags")
        check(fields.startsWith("$word\u001fto eat"), "the fields of text arrive as they are")
        check(Regex("<img src=\"oretsuba_${stamp}[^\"]*\\.jpe?g\">").containsMatchIn(fields), "the picture is in its field as an image")
        // AnkiDroid names a stored file's ending after its type: Ogg sound becomes .oga, Ogg's own ending for sound alone.
        val stored = Regex("\\[sound:(oretsuba_${stamp}[^\\]]*)\\]").findAll(fields).map { it.groupValues[1] }.toList()
        check(stored.any { it.endsWith(".ogg") || it.endsWith(".oga") }, "the .ogg sound is in its field, as .ogg or .oga (stored: $stored)")
        for (ending in listOf("m4a", "mp3")) {
            check(stored.any { it.endsWith(".$ending") }, "the .$ending sound is in its field and keeps its ending")
        }
        check(stored.size == 3, "three sounds in all")
        check(tags.split(' ').filter { it.isNotEmpty() }.toSet() == setOf("oretsuba-check", "two_words"),
            "the tags arrive, a blank inside one made an underscore")

        var deckId = -1L
        resolver.query(Uri.parse("content://com.ichi2.anki.flashcards/decks"), null, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                if (rows.getString(rows.getColumnIndexOrThrow("deck_name")) == deck) deckId = rows.getLong(rows.getColumnIndexOrThrow("deck_id"))
            }
        }
        var cards = 0
        var inDeck = 0
        resolver.query(Uri.withAppendedPath(Uri.withAppendedPath(notes, id), "cards"), null, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                cards++
                if (rows.getLong(rows.getColumnIndexOrThrow("deck_id")) == deckId) inDeck++
            }
        }
        check(cards > 0 && cards == inDeck, "the note's cards ($cards) are in the deck \"$deck\"")

        // ---- the same word again
        check(known(context, word), "the word is known now")
        check(!known(context, "$word!"), "a longer word is not")
        val again = ask(context, "add", card(word, "again"))
        check(again.optString("code") == "duplicate", "a second card of the word is refused as a duplicate (${again.optString("error")})")
        val forced = ask(context, "add", card(word, "again").put("allowDuplicate", true))
        check(forced.optString("id").isNotEmpty(), "unless duplicates are asked for")

        // ---- words with the marks an Anki search reads
        val marks = "a*b_c:d\\e \"f\" (g) $stamp"
        check(ask(context, "add", card(marks)).optString("id").isNotEmpty(), "a word full of a search's marks is added")
        check(known(context, marks), "and found again exactly")
        check(!known(context, marks.replace("a*b", "aXXb")) && !known(context, marks.replace("b_c", "bXc")),
            "its * and _ are letters, not wildcards")
        val bold = "<b>太字</b> $stamp"
        check(ask(context, "add", card(bold)).optString("id").isNotEmpty(), "a word in bold is added")
        check(known(context, bold), "and known by its HTML")

        // ---- what AnkiDroid cannot take
        check(ask(context, "add", card("x $stamp", toDeck = "No such deck $stamp")).optString("code") == "deck", "a deck that is not there is said so")
        check(ask(context, "add", card("y $stamp", type = "No such type $stamp")).optString("code") == "noteType", "and so is a note type")
        check(ask(context, "add", card("  ")).optString("code") == "empty", "a card without its first field is refused as empty")
        check(ask(context, "nonsense").optString("code") == "failed", "a request it does not know fails")
    }
}

// Plays the player on the mining settings screen (MiningSettingsActivity) with
// the real AnkiDroid on a device or an emulator: sets "Send cards to" to
// AnkiDroid, answers Android's question, chooses a deck, a note type and its
// fields, and makes a card the way the dictionary's + does, through the mining
// kit; at the end it gives AnkiDroid a note type with the fields of Lapis and
// looks whether the screen takes it together with its deck. Only debug builds
// hold it. With AnkiDroid installed and opened once, and this app not yet let
// at its cards:
//
//   adb shell am instrument -w io.github.friedrich_de.oretsuba/.MiningSettingsCheck
//
// It prints one line for each thing it checked and ends with "passed" or
// "FAILED", and leaves pictures of the screen in the app's own folder
// (Android/data/io.github.friedrich_de.oretsuba/files/check).
//
//   -e refuse twice     says no to Android's question twice, so that Android stops
//                       asking, and takes the way through the system's settings
//   -e audioPort PORT   the port of an audio source on the computer an emulator
//                       runs on (tools/mining_page/neighbours.mjs): the card then
//                       gets the word's sound
//   -e roles A,B,C      what the note type's first fields get, by the kit's names
//                       (word,meaning,wordAudio without it; word,picture,sound
//                       leaves settings with which a card made in the game
//                       carries the picture and the line's voice; the part
//                       with Lapis is left out then, so that they stay)
//   -e only notes       checks nothing: prints the notes AnkiDroid got in the
//                       last day, newest first, as this app is let see them
//
// It adds notes, the decks "Oretsuba check" and "Oretsuba Lapis" and a note
// type "Lapis" to AnkiDroid and changes this app's mining settings, so run it
// where neither matters.
package io.github.friedrich_de.oretsuba

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.AccessibilityServiceInfo
import android.app.Activity
import android.app.Instrumentation
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.util.Log
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Button
import android.widget.Spinner
import android.widget.TextView
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

class MiningSettingsCheck : Instrumentation() {
    private val report = StringBuilder()
    private var failed = 0
    private var refuseTwice = false
    private var audioPort = ""
    private var roleNames = listOf("word", "meaning", "wordAudio")
    private var rolesGiven = false
    private var onlyNotes = false
    private var shots: File? = null
    private var shot = 0

    override fun onCreate(arguments: Bundle?) {
        super.onCreate(arguments)
        refuseTwice = arguments?.getString("refuse") == "twice"
        audioPort = arguments?.getString("audioPort").orEmpty()
        arguments?.getString("roles")?.split(',')?.map { it.trim() }?.filter { it.isNotEmpty() }?.takeIf { it.isNotEmpty() }
            ?.let {
                roleNames = it
                rolesGiven = true
            }
        onlyNotes = arguments?.getString("only") == "notes"
        start()
    }

    override fun onStart() {
        try {
            if (onlyNotes) notes(targetContext) else run(targetContext)
        } catch (e: Throwable) {
            check(false, "the check runs to its end ($e)")
            Log.w(TAG, e)
            picture("it-ended-here")
            note("on the screen: " + texts().joinToString(" | "))
        }
        say(if (failed == 0) "Mining settings check passed" else "Mining settings check FAILED: $failed")
        val result = Bundle()
        result.putString(REPORT_KEY_STREAMRESULT, report.toString())
        finish(if (failed == 0) Activity.RESULT_OK else Activity.RESULT_CANCELED, result)
    }

    // ---------------------------------------------------------------- telling

    private fun say(line: String) {
        report.append(line).append('\n')
        Log.i(TAG, line)
    }

    private fun check(ok: Boolean, what: String) {
        say((if (ok) "  ok    " else "  FAIL  ") + what)
        if (!ok) failed++
    }

    private fun note(text: String) = say("        $text")

    /** A picture of the whole screen, numbered in the order they were taken. */
    private fun picture(name: String) {
        val folder = shots ?: return
        try {
            // A list that just closed is still fading away.
            SystemClock.sleep(600)
            val image: Bitmap = uiAutomation.takeScreenshot() ?: return
            shot++
            File(folder, "%02d-%s.png".format(shot, name)).outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) }
        } catch (e: Throwable) {
            note("no picture of $name: $e")
        }
    }

    // ---------------------------------------------------------------- the screen, as the system describes it

    private fun roots(): List<AccessibilityNodeInfo> {
        val found = ArrayList<AccessibilityNodeInfo>()
        try {
            for (window in uiAutomation.windows) window.root?.let { found.add(it) }
        } catch (e: Throwable) {
            // Answered below with the window in front alone.
        }
        if (found.isEmpty()) uiAutomation.rootInActiveWindow?.let { found.add(it) }
        return found
    }

    private fun walk(node: AccessibilityNodeInfo, into: MutableList<AccessibilityNodeInfo>) {
        if (into.size > 4000) return
        into.add(node)
        for (i in 0 until node.childCount) node.getChild(i)?.let { walk(it, into) }
    }

    private fun nodes(): List<AccessibilityNodeInfo> {
        val all = ArrayList<AccessibilityNodeInfo>()
        for (root in roots()) walk(root, all)
        return all
    }

    private fun texts(): List<String> = nodes().mapNotNull { it.text?.toString()?.takeIf { text -> text.isNotBlank() } }

    private fun find(test: (AccessibilityNodeInfo) -> Boolean): AccessibilityNodeInfo? = nodes().firstOrNull(test)

    private fun withText(text: String): (AccessibilityNodeInfo) -> Boolean = { it.text?.toString() == text }

    /** The lists of the dialog on the screen (one with a title), in their order. */
    private fun dialogLists(): List<AccessibilityNodeInfo> {
        for (root in roots()) {
            val all = ArrayList<AccessibilityNodeInfo>()
            walk(root, all)
            if (all.any(withId("alertTitle"))) return all.filter { it.className?.toString() == "android.widget.Spinner" }
        }
        return emptyList()
    }

    private fun withId(vararg endings: String): (AccessibilityNodeInfo) -> Boolean = { node ->
        val id = node.viewIdResourceName
        id != null && endings.any { id.endsWith(":id/$it") }
    }

    private fun press(node: AccessibilityNodeInfo): Boolean {
        var target: AccessibilityNodeInfo? = node
        while (target != null && !target.isClickable) target = target.parent
        return (target ?: node).performAction(AccessibilityNodeInfo.ACTION_CLICK)
    }

    /** Waits until [test] holds; false when it does not within [seconds]. */
    private fun waitFor(seconds: Int = 20, test: () -> Boolean): Boolean {
        val end = SystemClock.uptimeMillis() + seconds * 1000L
        while (true) {
            val holds = try {
                test()
            } catch (e: Throwable) {
                false
            }
            if (holds) return true
            if (SystemClock.uptimeMillis() > end) return false
            SystemClock.sleep(200)
        }
    }

    /** Presses what [test] finds, as soon as it is on the screen. */
    private fun pressWhenThere(seconds: Int = 20, test: (AccessibilityNodeInfo) -> Boolean): Boolean {
        var pressed = false
        waitFor(seconds) {
            val node = find(test)
            pressed = node != null && press(node)
            pressed
        }
        return pressed
    }

    /**
     * Presses the entry [text] of a list that has just dropped down. A long
     * list shows only its first entries: it is scrolled until the entry is there.
     */
    private fun pick(text: String): Boolean {
        for (attempt in 0 until 8) {
            if (pressWhenThere(if (attempt == 0) 4 else 2, withText(text))) return true
            val list = find { it.isScrollable && it.className?.toString()?.contains("ListView") == true } ?: return false
            if (!list.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD)) return false
            SystemClock.sleep(400)
        }
        return false
    }

    // ---------------------------------------------------------------- the screen, from inside

    private fun <T> onMain(work: () -> T): T {
        var result: T? = null
        runOnMainSync { result = work() }
        @Suppress("UNCHECKED_CAST")
        return result as T
    }

    private fun shown(activity: Activity, id: Int): Boolean = onMain { activity.findViewById<View>(id).isShown }

    private fun text(activity: Activity, id: Int): String = onMain { activity.findViewById<TextView>(id).text.toString() }

    private fun entries(activity: Activity, id: Int): List<String> = onMain {
        val adapter = activity.findViewById<Spinner>(id).adapter
        if (adapter == null) emptyList() else (0 until adapter.count).map { adapter.getItem(it).toString() }
    }

    private fun chosen(activity: Activity, id: Int): String =
        onMain { activity.findViewById<Spinner>(id).selectedItem?.toString().orEmpty() }

    /** Opens a list of the screen and presses one of its entries. */
    private fun choose(activity: Activity, id: Int, entry: String): Boolean {
        onMain {
            val spinner = activity.findViewById<Spinner>(id)
            spinner.requestRectangleOnScreen(android.graphics.Rect(0, 0, spinner.width, spinner.height), true)
            spinner.performClick()
        }
        return pick(entry)
    }

    private fun pressButton(activity: Activity, id: Int) = onMain {
        val button = activity.findViewById<Button>(id)
        button.requestRectangleOnScreen(android.graphics.Rect(0, 0, button.width, button.height), true)
        button.performClick()
    }

    private fun settings(): JSONObject = (MiningKit.call("settings.get") as? JSONObject)?.optJSONObject("anki") ?: JSONObject()

    private fun fieldRoles(): Map<String, String> {
        val list = settings().optJSONArray("fields") ?: JSONArray()
        return (0 until list.length()).mapNotNull { list.optJSONObject(it) }.associate { it.optString("field") to it.optString("role") }
    }

    // ---------------------------------------------------------------- Android's question

    private val allow = withId("permission_allow_button")
    private val deny = withId("permission_deny_button", "permission_deny_and_dont_ask_again_button")

    /** Waits for Android's question about AnkiDroid and answers it. */
    private fun answerQuestion(yes: Boolean, name: String): Boolean {
        if (!waitFor(20) { find(allow) != null }) {
            picture("no-question")
            note("on the screen: " + texts().joinToString(" | "))
            return false
        }
        find(withId("permission_message"))?.text?.let { note("Android asks: $it") }
        note("its answers: " + nodes().filter { it.isClickable && !it.text.isNullOrBlank() &&
                                                 it.packageName?.toString()?.contains("permissioncontroller") == true }
            .joinToString(" / ") { it.text.toString() })
        picture(name)
        val node = find(if (yes) allow else deny) ?: return false
        return press(node)
    }

    // ---------------------------------------------------------------- the check

    /** The notes AnkiDroid got in the last day, newest first: their fields and tags. */
    private fun notes(context: Context) {
        val found = ArrayList<Pair<Long, String>>()
        context.contentResolver.query(Uri.parse("content://com.ichi2.anki.flashcards/notes"), null, "added:1", null, null)?.use { rows ->
            val id = rows.getColumnIndexOrThrow("_id")
            val fields = rows.getColumnIndexOrThrow("flds")
            val tags = rows.getColumnIndexOrThrow("tags")
            while (rows.moveToNext()) {
                found.add(rows.getLong(id) to (rows.getString(fields) ?: "").replace('\u001f', '|') + "   tags:" + (rows.getString(tags) ?: ""))
            }
        }
        note("${found.size} notes of the last day in AnkiDroid")
        for ((id, text) in found.sortedByDescending { it.first }.take(12)) note("$id  $text")
    }

    private fun run(context: Context) {
        shots = context.getExternalFilesDir("check")?.also { folder ->
            folder.mkdirs()
            folder.listFiles()?.forEach { it.delete() }
        }
        // All the windows on the screen, not only the one in front: a list that dropped down and Android's question are windows of their own.
        try {
            val automation = uiAutomation
            automation.serviceInfo = automation.serviceInfo.apply { flags = flags or AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS }
        } catch (e: Throwable) {
            note("only the window in front can be read: $e")
        }

        check(AnkiDroid.installed(context), "AnkiDroid is on this device")
        if (!AnkiDroid.installed(context)) return
        val allowedBefore = AnkiDroid.allowed(context)
        if (allowedBefore) note("this app is let at AnkiDroid's cards already: Android's question will not come")

        // ---- the settings as a new player finds them
        check(MiningKit.open(context), "the mining kit opens (${MiningKit.problem})")
        MiningKit.call("settings.set", JSONObject().put("anki", JSONObject().put("target", "connect")))
        MiningKit.call("anki.choose", JSONObject().put("deck", "").put("noteType", ""))
        val places = MiningKit.call("anki.targets") as? JSONArray ?: JSONArray()
        val names = (0 until places.length()).map { places.getJSONObject(it).getString("name") }
        check(names == listOf("connect", "ankidroid"), "the kit names two places for cards here: $names")

        val start = Intent(context, MiningSettingsActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val activity = startActivitySync(start)
        check(waitFor { shown(activity, R.id.anki_target) }, "the screen shows \"Send cards to\"")
        check(entries(activity, R.id.anki_target_choice) == listOf("AnkiConnect", "AnkiDroid"),
              "it offers AnkiConnect and AnkiDroid: ${entries(activity, R.id.anki_target_choice)}")
        check(chosen(activity, R.id.anki_target_choice) == "AnkiConnect", "AnkiConnect is chosen at first")
        check(shown(activity, R.id.anki_connect) && !shown(activity, R.id.anki_droid),
              "with AnkiConnect the address, the key and Connect are there")
        picture("anki-connect")

        // ---- AnkiDroid is chosen, and Android asks
        check(choose(activity, R.id.anki_target_choice, "AnkiDroid"), "AnkiDroid is chosen in the list")
        if (!allowedBefore) {
            if (refuseTwice) refuseTwice(context, activity) else refuseOnce(context, activity)
        }

        // ---- AnkiDroid answers
        // The screen's own words first: an AnkiConnect that answered at the address kept from before may have a deck Default too.
        val answers = listOf(context.getString(R.string.kit_droid_plain), context.getString(R.string.kit_droid_lapis))
        check(waitFor(30) { text(activity, R.id.anki_check) in answers && entries(activity, R.id.deck).contains("Default") },
              "AnkiDroid's decks are in the list: ${entries(activity, R.id.deck)}")
        val types = entries(activity, R.id.note_type)
        check(types.contains("Basic"), "and its note types: $types")
        val said = text(activity, R.id.anki_check)
        check(said == context.getString(R.string.kit_droid_plain) || said == context.getString(R.string.kit_droid_lapis),
              "the screen says that AnkiDroid answers: \"$said\"")
        check(!shown(activity, R.id.anki_connect) && shown(activity, R.id.anki_droid) && !shown(activity, R.id.anki_forget_key),
              "the address, the key and Connect are gone")
        check(text(activity, R.id.anki_droid_button) == context.getString(R.string.kit_droid_again),
              "the button now fetches the lists again")
        check(text(activity, R.id.anki_note) == context.getString(R.string.kit_droid_note), "the note below is about AnkiDroid")
        check(settings().optString("target") == "ankidroid", "the kit keeps AnkiDroid as the place for cards")
        picture("ankidroid-answers")

        // ---- a deck, a note type, its fields
        check(choose(activity, R.id.deck, "Default") && waitFor { settings().optString("deck") == "Default" }, "the deck Default is chosen")
        // A note type with a third field, where there is one, so that the word's sound has a field of its own.
        val type = types.firstOrNull { it.contains("optional reversed") } ?: "Basic"
        check(choose(activity, R.id.note_type, type) && waitFor { settings().optString("noteType") == type },
              "the note type \"$type\" is chosen")
        val info = JSONObject(AnkiDroid.call(context, "info", "{}")).optJSONArray("noteTypes") ?: JSONArray()
        val fields = (0 until info.length()).map { info.getJSONObject(it) }.firstOrNull { it.getString("name") == type }
            ?.getJSONArray("fields")?.let { list -> (0 until list.length()).map { list.getString(it) } } ?: emptyList()
        check(waitFor { fieldRoles().keys.toList() == fields } && fields.size >= 2, "the kit has its fields as AnkiDroid names them: $fields")
        check(waitFor { shown(activity, R.id.card_problem) }, "the screen says what a card still needs: \"${text(activity, R.id.card_problem)}\"")

        val labels = HashMap<String, String>()
        val roles = MiningKit.call("anki.roleChoices") as? JSONArray ?: JSONArray()
        for (i in 0 until roles.length()) roles.getJSONObject(i).let { labels[it.getString("role")] = it.getString("label") }
        val wanted = roleNames.take(fields.size)
        pressButton(activity, R.id.anki_fields)
        check(waitFor { find(withText(fields[0])) != null }, "the fields dialog opens")
        for ((index, role) in wanted.withIndex()) {
            // Asked for anew each time: what the system told of the dialog before a choice no longer holds after it.
            val list = dialogLists().getOrNull(index)
            val picked = list != null && press(list) && pick(labels[role].orEmpty())
            check(picked && waitFor { fieldRoles()[fields[index]] == role }, "${fields[index]} gets ${labels[role]}")
        }
        picture("fields")
        check(pressWhenThere(10, withId("button1")), "the dialog is closed with Done")
        check(waitFor { !shown(activity, R.id.card_problem) }, "nothing is missing for a card any more")
        val page = MiningKit.call("page.init") as? JSONObject
        check(page?.optJSONObject("cards")?.optBoolean("possible") == true, "the dictionary is told that cards can be made")
        picture("ready")

        // ---- a card, as the + of the dictionary makes it
        if (audioPort.isNotEmpty()) {
            val address = "http://10.0.2.2:$audioPort/list?term={term}&reading={reading}"
            val sounds = try {
                (MiningKit.call("audio.check", JSONObject().put("address", address)) as? JSONObject)?.optJSONArray("names")?.length() ?: 0
            } catch (e: KitError) {
                note("the audio source: ${e.message}")
                0
            }
            check(sounds > 0, "the audio source on the computer answers")
            MiningKit.setSecret(Secrets.AUDIO_ADDRESS, address)
        }
        val stamp = System.currentTimeMillis().toString(36)
        val word = JSONObject().put("expression", "食べる$stamp").put("reading", "たべる").put("meaning", "to eat ($stamp)")
        val other = JSONObject().put("expression", "飲む$stamp").put("reading", "のむ").put("meaning", "to drink")
        fun known(): List<Boolean> {
            val answer = MiningKit.call("card.known", JSONObject().put("words", JSONArray().put(word).put(other))) as? JSONObject
            val list = answer?.optJSONArray("known") ?: JSONArray()
            return (0 until list.length()).map { list.opt(it) == true }
        }
        check(known() == listOf(false, false), "before the card, AnkiDroid does not know the word: ${known()}")
        val made = try {
            MiningKit.call("card.add", JSONObject().put("word", word)) as? JSONObject
        } catch (e: KitError) {
            note("card.add: ${e.message} (${e.kind})")
            null
        }
        note("card.add answered: $made")
        val id = made?.optLong("noteId") ?: 0L
        check(made != null && id > 0 && !made.optBoolean("handedOver"), "the kit makes a card in AnkiDroid at once and has its number")
        if (id > 0) {
            val notes = Uri.parse("content://com.ichi2.anki.flashcards/notes")
            var kept = ""
            context.contentResolver.query(Uri.withAppendedPath(notes, id.toString()), null, null, null, null)?.use { row ->
                if (row.moveToFirst()) kept = row.getString(row.getColumnIndexOrThrow("flds")) ?: ""
            }
            val values = kept.split('\u001f')
            note("the note's fields: " + values.joinToString(" | "))
            if (wanted.getOrNull(0) == "word") check(values.getOrNull(0) == "食べる$stamp", "the word is in the first field")
            if (wanted.getOrNull(1) == "meaning") check(values.getOrNull(1)?.contains("to eat ($stamp)") == true, "its meaning is in the second")
            if (audioPort.isNotEmpty() && wanted.getOrNull(2) == "wordAudio") {
                check(Regex("^\\[sound:[^\\]]+\\]$").matches(values.getOrNull(2).orEmpty()),
                      "the word's sound is in the third, under the name AnkiDroid stored it by")
                check(made?.optBoolean("noWordAudio") == false, "and the kit does not say the sound was missing")
            }
            var deckId = -1L
            context.contentResolver.query(Uri.parse("content://com.ichi2.anki.flashcards/decks"), null, null, null, null)?.use { rows ->
                while (rows.moveToNext()) {
                    if (rows.getString(rows.getColumnIndexOrThrow("deck_name")) == "Default") deckId = rows.getLong(rows.getColumnIndexOrThrow("deck_id"))
                }
            }
            var cards = 0
            var inDeck = 0
            context.contentResolver.query(Uri.withAppendedPath(Uri.withAppendedPath(notes, id.toString()), "cards"), null, null, null, null)?.use { rows ->
                while (rows.moveToNext()) {
                    cards++
                    if (rows.getLong(rows.getColumnIndexOrThrow("deck_id")) == deckId) inDeck++
                }
            }
            check(cards > 0 && cards == inDeck, "its cards ($cards) are in the deck Default")
        }
        check(known() == listOf(true, false), "now the word is marked as in Anki, another is not: ${known()}")
        val again = try {
            MiningKit.call("card.add", JSONObject().put("word", word))
            "a second card was made"
        } catch (e: KitError) {
            e.kind
        }
        check(again == "duplicate", "a second card of the word is refused as a duplicate ($again)")

        // ---- a deck made in AnkiDroid meanwhile is in the list after coming back
        val deck = "Oretsuba check"
        if (entries(activity, R.id.deck).contains(deck)) {
            note("the deck \"$deck\" is there from an earlier run")
        } else {
            val row = ContentValues()
            row.put("deck_name", deck)
            check(context.contentResolver.insert(Uri.parse("content://com.ichi2.anki.flashcards/decks"), row) != null,
                  "a deck is made in AnkiDroid behind the screen's back")
        }
        onMain {
            callActivityOnPause(activity)
            callActivityOnResume(activity)
        }
        check(waitFor { entries(activity, R.id.deck).contains(deck) }, "after coming back to the screen the new deck is in the list")
        check(chosen(activity, R.id.deck) == "Default", "and the chosen deck is still chosen")

        // ---- and back to an AnkiConnect, and to AnkiDroid again
        check(choose(activity, R.id.anki_target_choice, "AnkiConnect") && waitFor { settings().optString("target") == "connect" },
              "AnkiConnect is chosen again")
        check(waitFor { shown(activity, R.id.anki_connect) && !shown(activity, R.id.anki_droid) }, "its address and key are back")
        // Without an AnkiConnect the list holds the chosen deck alone; one that answers at a kept address fills it with its own.
        check(!entries(activity, R.id.deck).contains(deck) && chosen(activity, R.id.deck) == "Default",
              "AnkiDroid's decks are out of the list, the chosen one stays named: ${entries(activity, R.id.deck)}")
        picture("anki-connect-again")
        check(choose(activity, R.id.anki_target_choice, "AnkiDroid") &&
                  waitFor(30) { text(activity, R.id.anki_check) in answers && entries(activity, R.id.deck).contains(deck) },
              "AnkiDroid again: its lists are there at once, and Android does not ask a second time")
        picture("ankidroid-again")

        if (!rolesGiven) lapis(context, activity)
        onMain { activity.finish() }
    }

    private fun deckId(context: Context, name: String): Long {
        var id = -1L
        context.contentResolver.query(Uri.parse("content://com.ichi2.anki.flashcards/decks"), null, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                if (rows.getString(rows.getColumnIndexOrThrow("deck_name")) == name) id = rows.getLong(rows.getColumnIndexOrThrow("deck_id"))
            }
        }
        return id
    }

    /**
     * The Lapis note type comes with its deck. A note type with Lapis's fields
     * and a card of it in a deck of its own are made in AnkiDroid behind the
     * screen's back; with neither deck nor note type chosen, the screen then
     * fetches the lists again. AnkiDroid has three decks by now, so the deck
     * can only be the one that card is in.
     */
    private fun lapis(context: Context, activity: Activity) {
        val resolver = context.contentResolver
        val type = "Lapis"
        val deck = "Oretsuba Lapis"
        val info = JSONObject(AnkiDroid.call(context, "info", "{}"))
        val types = info.optJSONArray("noteTypes") ?: JSONArray()
        if ((0 until types.length()).any { types.getJSONObject(it).getString("name") == type }) {
            note("the note type \"$type\" is there from an earlier run")
        } else {
            val row = ContentValues()
            row.put("name", type)
            row.put("field_names", LAPIS_FIELDS.joinToString("\u001f"))
            row.put("num_cards", 1)
            val made = try {
                resolver.insert(Uri.parse("content://com.ichi2.anki.flashcards/models"), row)
            } catch (e: Throwable) {
                note("making the note type: $e")
                null
            }
            check(made != null, "a note type with the fields of Lapis is made in AnkiDroid")
            if (made == null) return
        }
        val decks = info.optJSONArray("decks") ?: JSONArray()
        if ((0 until decks.length()).none { decks.getString(it) == deck }) {
            val row = ContentValues()
            row.put("deck_name", deck)
            check(resolver.insert(Uri.parse("content://com.ichi2.anki.flashcards/decks"), row) != null, "and a deck \"$deck\"")
        }
        val stamp = System.currentTimeMillis().toString(36)
        val first = JSONObject(AnkiDroid.call(context, "add", JSONObject().put("deck", deck).put("noteType", type)
            .put("fields", JSONObject().put("Expression", "読む$stamp")).toString()))
        check(first.optString("id").isNotEmpty(), "with a card of that note type in it (${first.optString("error")})")
        val newest = JSONObject(AnkiDroid.call(context, "deckOfNewest", JSONObject().put("noteType", type).toString()))
        check(newest.optString("deck") == deck, "AnkiDroid names the deck the note type's newest card is in: $newest")
        val none = JSONObject(AnkiDroid.call(context, "deckOfNewest", JSONObject().put("noteType", "Cloze").toString()))
        check(none.has("deck") && none.optString("deck").isEmpty(), "and none for a note type without a card: $none")

        MiningKit.call("anki.choose", JSONObject().put("deck", "").put("noteType", ""))
        pressButton(activity, R.id.anki_droid_button)
        check(waitFor(30) { chosen(activity, R.id.note_type) == type && chosen(activity, R.id.deck) == deck },
              "with nothing chosen, fetching the lists again takes Lapis and its deck: " +
                  "\"${chosen(activity, R.id.note_type)}\" in \"${chosen(activity, R.id.deck)}\" of ${entries(activity, R.id.deck)}")
        check(text(activity, R.id.anki_check) == context.getString(R.string.kit_droid_lapis),
              "the screen says that the Lapis note type is there: \"${text(activity, R.id.anki_check)}\"")
        val roles = fieldRoles()
        check(roles["Expression"] == "word" && roles["ExpressionReading"] == "reading" && roles["MainDefinition"] == "meaning" &&
                  roles["Sentence"] == "sentence" && roles["Picture"] == "picture" && roles["SentenceAudio"] == "sound" &&
                  roles["ExpressionAudio"] == "wordAudio",
              "its fields are filled the way Lapis means them")
        check(waitFor { !shown(activity, R.id.card_problem) } &&
                  (MiningKit.call("page.init") as? JSONObject)?.optJSONObject("cards")?.optBoolean("possible") == true,
              "and nothing is left to choose for a card")
        picture("lapis")

        val word = JSONObject().put("expression", "書く$stamp").put("reading", "かく").put("meaning", "to write ($stamp)")
        val made = try {
            MiningKit.call("card.add", JSONObject().put("word", word)) as? JSONObject
        } catch (e: KitError) {
            note("card.add: ${e.message} (${e.kind})")
            null
        }
        val id = made?.optLong("noteId") ?: 0L
        check(id > 0, "the kit makes a card of the Lapis note type: $made")
        if (id <= 0) return
        val notes = Uri.parse("content://com.ichi2.anki.flashcards/notes")
        var kept = ""
        resolver.query(Uri.withAppendedPath(notes, id.toString()), null, null, null, null)?.use { row ->
            if (row.moveToFirst()) kept = row.getString(row.getColumnIndexOrThrow("flds")) ?: ""
        }
        val values = kept.split('\u001f')
        fun value(field: String): String = values.getOrNull(LAPIS_FIELDS.indexOf(field)).orEmpty()
        note("the note: Expression \"${value("Expression")}\", ExpressionReading \"${value("ExpressionReading")}\", " +
                 "MainDefinition \"${value("MainDefinition")}\", ExpressionAudio \"${value("ExpressionAudio")}\"")
        check(value("Expression") == "書く$stamp" && value("ExpressionReading") == "かく" && value("MainDefinition").contains("to write ($stamp)"),
              "the word, its reading and its meaning are in the fields Lapis has for them")
        if (audioPort.isNotEmpty()) {
            check(Regex("^\\[sound:[^\\]]+\\]$").matches(value("ExpressionAudio")), "and the word's sound in ExpressionAudio")
        }
        val wanted = deckId(context, deck)
        var cards = 0
        var inDeck = 0
        resolver.query(Uri.withAppendedPath(Uri.withAppendedPath(notes, id.toString()), "cards"), null, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                cards++
                if (rows.getLong(rows.getColumnIndexOrThrow("deck_id")) == wanted) inDeck++
            }
        }
        check(cards > 0 && cards == inDeck, "its cards ($cards) are in the deck \"$deck\"")
    }

    /** No to Android's question, then yes when the screen's button asks again. */
    private fun refuseOnce(context: Context, activity: Activity) {
        check(answerQuestion(false, "android-asks"), "Android asks whether this app may read and write AnkiDroid's cards; the answer is no")
        check(waitFor { text(activity, R.id.anki_check) == context.getString(R.string.kit_droid_not_allowed) },
              "the screen says that AnkiDroid's cards are closed: \"${text(activity, R.id.anki_check)}\"")
        check(text(activity, R.id.anki_droid_button) == context.getString(R.string.kit_droid_allow), "its button asks again")
        check(!shown(activity, R.id.deck), "there is no deck to choose")
        picture("not-allowed")
        pressButton(activity, R.id.anki_droid_button)
        check(answerQuestion(true, "android-asks-again"), "Android asks again; the answer is yes")
        check(waitFor { AnkiDroid.allowed(context) }, "this app is let at AnkiDroid's cards")
    }

    /** No twice, after which Android stops asking: the way left is the system's settings. */
    private fun refuseTwice(context: Context, activity: Activity) {
        check(answerQuestion(false, "android-asks"), "Android asks whether this app may read and write AnkiDroid's cards; the answer is no")
        check(waitFor { text(activity, R.id.anki_check) == context.getString(R.string.kit_droid_not_allowed) },
              "the screen says that AnkiDroid's cards are closed")
        pressButton(activity, R.id.anki_droid_button)
        check(answerQuestion(false, "android-asks-again"), "Android asks again; the answer is no again")
        check(waitFor { text(activity, R.id.anki_check) == context.getString(R.string.kit_droid_refused) },
              "the screen says that Android no longer asks: \"${text(activity, R.id.anki_check)}\"")
        check(text(activity, R.id.anki_droid_button) == context.getString(R.string.kit_droid_permissions),
              "its button leads to the system's settings")
        picture("android-no-longer-asks")
        pressButton(activity, R.id.anki_droid_button)
        check(waitFor { find { it.packageName?.toString()?.contains("settings") == true } != null },
              "the system's settings for this app come to the front")
        picture("system-settings")
        // The player's way from there: Permissions, Additional permissions, the permission by the name AnkiDroid gave it, Allow.
        var tapped = false
        if (pressWhenThere(5, withText("Permissions"))) {
            SystemClock.sleep(1500)
            picture("system-permissions")
            note("the system's permissions screen: " + texts().joinToString(" | "))
            val label = try {
                context.packageManager.getPermissionInfo(AnkiDroid.PERMISSION, 0).loadLabel(context.packageManager).toString()
            } catch (e: Throwable) {
                ""
            }
            note("AnkiDroid calls the permission \"$label\"")
            if (pressWhenThere(5, withText("Additional permissions"))) {
                SystemClock.sleep(1500)
                picture("system-additional-permissions")
                note("under Additional permissions: " + texts().joinToString(" | "))
                if (label.isNotEmpty() && pressWhenThere(5) { it.text?.toString().equals(label, ignoreCase = true) }) {
                    SystemClock.sleep(1500)
                    note("the permission's own screen: " + texts().joinToString(" | "))
                    tapped = pressWhenThere(5, withId("allow_radio_button")) && waitFor(5) { AnkiDroid.allowed(context) }
                    picture("system-permission-allowed")
                }
            }
        }
        if (tapped) {
            check(true, "the permission is given in the system's settings, under Permissions, Additional permissions")
        } else {
            // Where the taps do not find their way (another Android lays these screens out differently), the system's own means give it.
            note("the taps did not find the permission in the system's settings; it is given by the system's own means")
            if (Build.VERSION.SDK_INT >= 28) uiAutomation.grantRuntimePermission(context.packageName, AnkiDroid.PERMISSION)
            check(AnkiDroid.allowed(context), "the permission is given in the system's settings")
        }
        for (i in 0 until 6) {
            if (onMain { activity.hasWindowFocus() }) break
            uiAutomation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK)
            SystemClock.sleep(1200)
        }
        check(waitFor { onMain { activity.hasWindowFocus() } }, "Back leads to the mining settings again")
    }

    private companion object {
        const val TAG = "OretsubaCheck"

        /** The fields of the Lapis note type (github.com/donkuri/lapis), in its order. */
        val LAPIS_FIELDS = listOf(
            "Expression", "ExpressionFurigana", "ExpressionReading", "ExpressionAudio", "SelectionText", "MainDefinition",
            "DefinitionPicture", "Sentence", "SentenceFurigana", "SentenceAudio", "Picture", "Glossary", "Hint",
            "IsWordAndSentenceCard", "IsClickCard", "IsSentenceCard", "IsAudioCard", "PitchPosition", "PitchCategories",
            "Frequency", "FreqSort", "MiscInfo")
    }
}

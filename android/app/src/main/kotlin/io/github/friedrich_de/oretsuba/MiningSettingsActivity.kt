package io.github.friedrich_de.oretsuba

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import java.io.File
import java.text.NumberFormat
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject

/**
 * The launcher's screen for mining in the game (docs/MINING.md): whether the
 * text looks words up, where cards go, the dictionaries, the word audio and
 * how the popup looks. All of it lives in the mining kit; this asks and tells.
 *
 * The kit is asked away from the main thread ([kit]), because a call may wait
 * for the network; everything else here belongs to the main thread.
 */
class MiningSettingsActivity : Activity() {
    /** One thing a field of a card can hold: the word, the sentence, the picture ... */
    private class Role(val role: String, val label: String, val detail: String)

    private data class Installed(val id: String, val title: String, val terms: Int, val frequencies: Int,
                                 val pitches: Int, val enabled: Boolean)

    /** How asking a server went. */
    private class Check(val state: Int, val text: String = "")

    private val main = Handler(Looper.getMainLooper())

    // What the kit holds, as it was last heard.
    private var direct = false
    private var keySet = false
    private var audioSet = false
    private var deck = ""
    private var noteType = ""
    private var fields: List<Pair<String, String>> = emptyList()
    /** The tags as the kit holds them; the text field holds what the player is typing. */
    private var keptTags = ""
    private var decks: List<String> = emptyList()
    private var noteTypes: List<String> = emptyList()
    private var roles: List<Role> = emptyList()
    private var dictionaries: List<Installed> = emptyList()
    private var importState = "idle"
    private var importProgress = 0.0
    private var importTitle = ""
    private var importError = ""
    private var textSize = 100
    private var hideTags = false
    private var compact = false

    /** Why no card can be made as things are; empty when one can. */
    private var cardProblem = ""
    private var problem: String? = null
    /** Why this device cannot show the dictionary over the game; null when it can. */
    private var popupProblem: String? = null
    private var anki = Check(IDLE)
    private var audio = Check(IDLE)
    /** The name of a picked file while it is read into the app. */
    private var copying: String? = null
    private var watching = false
    private var shownDictionaries: List<Installed>? = null
    private var fieldSpinners: List<Pair<String, Spinner>> = emptyList()

    private lateinit var problemText: TextView
    private lateinit var directSwitch: Switch
    private lateinit var addressField: EditText
    private lateinit var keyField: EditText
    private lateinit var connectButton: Button
    private lateinit var ankiWorking: ProgressBar
    private lateinit var ankiCheck: TextView
    private lateinit var forgetKey: Button
    private lateinit var deckLabel: TextView
    private lateinit var deckSpinner: Spinner
    private lateinit var noteTypeLabel: TextView
    private lateinit var noteTypeSpinner: Spinner
    private lateinit var fieldsButton: Button
    private lateinit var tagsField: EditText
    private lateinit var cardProblemText: TextView
    private lateinit var dictionaryRows: LinearLayout
    private lateinit var importStateText: TextView
    private lateinit var importProgressBar: ProgressBar
    private lateinit var importStop: Button
    private lateinit var importFile: Button
    private lateinit var importAddress: Button
    private lateinit var importErrorText: TextView
    private lateinit var audioField: EditText
    private lateinit var audioButton: Button
    private lateinit var audioWorking: ProgressBar
    private lateinit var audioCheck: TextView
    private lateinit var forgetAudio: Button
    private lateinit var textSizeSpinner: Spinner
    private lateinit var hideTagsSwitch: Switch
    private lateinit var compactSwitch: Switch

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.mining_settings)
        problemText = findViewById(R.id.problem)
        directSwitch = findViewById(R.id.direct)
        addressField = findViewById(R.id.anki_address)
        keyField = findViewById(R.id.anki_key)
        connectButton = findViewById(R.id.anki_connect_button)
        ankiWorking = findViewById(R.id.anki_working)
        ankiCheck = findViewById(R.id.anki_check)
        forgetKey = findViewById(R.id.anki_forget_key)
        deckLabel = findViewById(R.id.deck_label)
        deckSpinner = findViewById(R.id.deck)
        noteTypeLabel = findViewById(R.id.note_type_label)
        noteTypeSpinner = findViewById(R.id.note_type)
        fieldsButton = findViewById(R.id.anki_fields)
        tagsField = findViewById(R.id.anki_tags)
        cardProblemText = findViewById(R.id.card_problem)
        dictionaryRows = findViewById(R.id.dictionaries)
        importStateText = findViewById(R.id.import_state)
        importProgressBar = findViewById(R.id.import_progress)
        importStop = findViewById(R.id.import_stop)
        importFile = findViewById(R.id.import_file)
        importAddress = findViewById(R.id.import_address)
        importErrorText = findViewById(R.id.import_error)
        audioField = findViewById(R.id.audio_address)
        audioButton = findViewById(R.id.audio_check_button)
        audioWorking = findViewById(R.id.audio_working)
        audioCheck = findViewById(R.id.audio_check)
        forgetAudio = findViewById(R.id.audio_forget)
        textSizeSpinner = findViewById(R.id.text_size)
        hideTagsSwitch = findViewById(R.id.hide_tags)
        compactSwitch = findViewById(R.id.compact)

        // A switch tells of every change, also of one made here to show what the kit holds:
        // only a state that differs from the kit's is the player's.
        directSwitch.setOnCheckedChangeListener { _, on ->
            if (on != direct) change("settings.set", JSONObject().put("direct", on))
        }
        hideTagsSwitch.setOnCheckedChangeListener { _, on ->
            if (on != hideTags) change("settings.set", JSONObject().put("popup", JSONObject().put("hideTags", on)))
        }
        compactSwitch.setOnCheckedChangeListener { _, on ->
            if (on != compact) change("settings.set", JSONObject().put("popup", JSONObject().put("compact", on)))
        }
        connectButton.setOnClickListener { connect(adopt = true) }
        onDone(addressField) { connect(adopt = true) }
        onDone(keyField) { connect(adopt = true) }
        forgetKey.setOnClickListener { forget(Secrets.ANKI_KEY) }
        fieldsButton.setOnClickListener { showFields() }
        onDone(tagsField) { setTags() }
        tagsField.setOnFocusChangeListener { _, focused -> if (!focused) setTags() }
        importFile.setOnClickListener {
            val pick = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            startActivityForResult(pick, REQUEST_DICTIONARY)
        }
        importAddress.setOnClickListener { askAddress() }
        importStop.setOnClickListener { dictionaryCall("dict.cancel") }
        audioButton.setOnClickListener { checkAudio() }
        onDone(audioField) { checkAudio() }
        forgetAudio.setOnClickListener { forget(Secrets.AUDIO_ADDRESS) }
        findViewById<Button>(R.id.done).setOnClickListener { finish() }

        if (!MiningKit.open(this)) {
            problem = MiningKit.problem
            show()
            return
        }
        load()
    }

    override fun onPause() {
        super.onPause()
        if (MiningKit.kit != 0L) setTags()
    }

    override fun onDestroy() {
        super.onDestroy()
        main.removeCallbacksAndMessages(null)
    }

    // The screen was turned or the window resized: the column takes its width for the new shape.
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        val column = findViewById<View>(R.id.column)
        val params = column.layoutParams
        params.width = resources.getDimensionPixelSize(R.dimen.column_width)
        column.layoutParams = params
    }

    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        if (request == REQUEST_DICTIONARY && result == RESULT_OK) data?.data?.let { importPicked(it) }
    }

    // ---------------------------------------------------------------- asking the kit

    /**
     * Runs [work] where it may wait, then [then] on the main thread with what
     * it gave, or with the kit's sentence when the kit refused.
     */
    private fun <T> kit(work: () -> T, then: (T?, String?) -> Unit) {
        MiningKit.work.execute {
            var result: T? = null
            var failure: String? = null
            try {
                result = work()
            } catch (e: KitError) {
                failure = e.message ?: ""
            }
            main.post { if (!isDestroyed) then(result, failure) }
        }
    }

    /** Whether a card could be made as things are: empty when it can, else why not. Waits like a call. */
    private fun askCards(): String? {
        val page = try {
            MiningKit.call("page.init") as? JSONObject
        } catch (e: KitError) {
            null
        }
        val cards = page?.optJSONObject("cards") ?: return null
        return if (cards.optBoolean("possible")) "" else cards.optString("problem")
    }

    private fun load() {
        kit({
            val loaded = JSONObject()
            loaded.put("roles", MiningKit.call("anki.roleChoices"))
            loaded.put("settings", MiningKit.call("settings.get"))
            loaded.put("list", MiningKit.call("dict.list"))
            loaded.put("cards", askCards())
            loaded
        }) { loaded, failure ->
            problem = failure
            popupProblem = MiningPopup.problem()?.let { getString(R.string.kit_old_web_view, it) }
            if (loaded != null) {
                val list = loaded.optJSONArray("roles") ?: JSONArray()
                roles = (0 until list.length()).mapNotNull { list.optJSONObject(it) }.map {
                    Role(it.optString("role"), it.optString("label"), it.optString("detail"))
                }
                loaded.optJSONObject("settings")?.let { take(it, typed = true) }
                loaded.optJSONObject("list")?.let { takeDictionaries(it) }
                if (loaded.has("cards")) cardProblem = loaded.optString("cards")
                // Copies of picked files that an app closed in the middle of an import left behind.
                if (!importRunning) removeCopies()
            }
            show()
            if (importRunning) watchImport()
            // The decks and note types to choose from, when an AnkiConnect is set already.
            if (addressField.text.isNotBlank()) connect(adopt = false)
        }
    }

    /**
     * Takes the settings as the kit holds them. What the player may be
     * typing, the address and the tags, is only replaced with [typed].
     */
    private fun take(settings: JSONObject, typed: Boolean = false) {
        direct = settings.optBoolean("direct")
        keySet = settings.optBoolean("ankiKeySet")
        audioSet = settings.optBoolean("audioSet")
        val card = settings.optJSONObject("anki") ?: JSONObject()
        val tags = card.optJSONArray("tags") ?: JSONArray()
        keptTags = (0 until tags.length()).joinToString(" ") { tags.optString(it) }
        if (typed) {
            addressField.setText(card.optString("address"))
            tagsField.setText(keptTags)
        }
        deck = card.optString("deck")
        noteType = card.optString("noteType")
        val list = card.optJSONArray("fields") ?: JSONArray()
        fields = (0 until list.length()).mapNotNull { list.optJSONObject(it) }.map {
            it.optString("field") to it.optString("role", "none")
        }
        val popup = settings.optJSONObject("popup") ?: JSONObject()
        textSize = popup.optInt("textSize", 100)
        hideTags = popup.optBoolean("hideTags")
        compact = popup.optBoolean("compact")
    }

    private fun takeDictionaries(answer: JSONObject) {
        val list = answer.optJSONArray("dictionaries") ?: JSONArray()
        dictionaries = (0 until list.length()).mapNotNull { list.optJSONObject(it) }.map {
            Installed(it.optString("id"), it.optString("title"), it.optInt("terms"), it.optInt("frequencies"),
                      it.optInt("pitches"), it.optBoolean("enabled", true))
        }
        val state = answer.optJSONObject("import") ?: JSONObject()
        importState = state.optString("state", "idle")
        importProgress = state.optDouble("progress", 0.0)
        importTitle = state.optString("title")
        importError = state.optString("error")
    }

    private val importRunning: Boolean get() = importState == "downloading" || importState == "importing"

    /** A call that answers with the settings as they are now. [after] runs once they are shown. */
    private fun change(method: String, params: JSONObject, after: (() -> Unit)? = null) {
        kit({ (MiningKit.call(method, params) as? JSONObject) to askCards() }) { answer, failure ->
            problem = failure
            answer?.first?.let { take(it) }
            answer?.second?.let { cardProblem = it }
            show()
            if (failure == null) after?.invoke()
        }
    }

    private fun refreshCards() {
        kit({ askCards() }) { cards, _ ->
            if (cards != null) {
                cardProblem = cards
                show()
            }
        }
    }

    // ---------------------------------------------------------------- showing

    private fun show() {
        val said = problem?.takeIf { it.isNotEmpty() } ?: popupProblem
        problemText.text = said
        problemText.visibility = if (said == null) View.GONE else View.VISIBLE
        directSwitch.isChecked = direct
        hideTagsSwitch.isChecked = hideTags
        compactSwitch.isChecked = compact

        keyField.setHint(if (keySet) R.string.kit_anki_key_kept else R.string.kit_anki_key)
        connectButton.isEnabled = anki.state != WORKING
        ankiWorking.visibility = if (anki.state == WORKING) View.VISIBLE else View.GONE
        showCheck(ankiCheck, anki)
        forgetKey.visibility = if (keySet) View.VISIBLE else View.GONE
        choice(deckSpinner, deckLabel, decks, deck) { change("anki.choose", JSONObject().put("deck", it)) }
        choice(noteTypeSpinner, noteTypeLabel, noteTypes, noteType) {
            change("anki.choose", JSONObject().put("noteType", it))
        }
        fieldsButton.visibility = if (fields.isEmpty()) View.GONE else View.VISIBLE
        for ((field, spinner) in fieldSpinners) markRole(field, spinner, fields.firstOrNull { it.first == field }?.second)
        val cards = cardProblem.isNotEmpty() && addressField.text.isNotBlank()
        cardProblemText.text = cardProblem
        cardProblemText.visibility = if (cards) View.VISIBLE else View.GONE

        showDictionaries()

        audioField.setHint(if (audioSet) R.string.kit_audio_address_kept else R.string.kit_audio_address)
        audioButton.setText(if (audioSet && audioField.text.isBlank()) R.string.kit_audio_check
                            else R.string.kit_audio_check_keep)
        audioButton.isEnabled = audio.state != WORKING
        audioWorking.visibility = if (audio.state == WORKING) View.VISIBLE else View.GONE
        showCheck(audioCheck, audio)
        forgetAudio.visibility = if (audioSet) View.VISIBLE else View.GONE

        val sizes = if (textSize in TEXT_SIZES) TEXT_SIZES else (TEXT_SIZES + textSize).sorted()
        fill(textSizeSpinner, sizes.map { it.toString() }, sizes.map { getString(R.string.kit_percent, it) },
             textSize.toString()) {
            change("settings.set", JSONObject().put("popup", JSONObject().put("textSize", it.toInt())))
        }
    }

    private fun showCheck(view: TextView, check: Check) {
        val said = check.state == GOOD || check.state == BAD
        view.visibility = if (said) View.VISIBLE else View.GONE
        view.text = check.text
        view.setTextColor(getColor(if (check.state == GOOD) R.color.good else R.color.warning))
    }

    /** A deck or a note type to choose, among those the Anki has; without any, the one that is set. */
    private fun choice(spinner: Spinner, label: TextView, all: List<String>, current: String, chosen: (String) -> Unit) {
        val values = if (current in all) all else listOf(current) + all
        val shown = all.isNotEmpty() || current.isNotEmpty()
        label.visibility = if (shown) View.VISIBLE else View.GONE
        spinner.visibility = if (shown) View.VISIBLE else View.GONE
        spinner.isEnabled = all.isNotEmpty()
        fill(spinner, values, values.map { it.ifEmpty { getString(R.string.kit_none_chosen) } }, current, chosen)
    }

    /**
     * Fills a list to choose from and marks what is chosen. A list tells of
     * every selection, also of the one made here, and only later: [chosen]
     * hears of a value that is not the one the list was last given.
     */
    private fun fill(spinner: Spinner, values: List<String>, labels: List<String>, current: String,
                     chosen: (String) -> Unit) {
        val state = values to current
        if (spinner.tag == state) return
        spinner.tag = state
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, labels)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        spinner.adapter = adapter
        spinner.setSelection(maxOf(0, values.indexOf(current)), false)
        spinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                val picked = values.getOrNull(position) ?: return
                val given = (spinner.tag as? Pair<*, *>)?.second
                if (picked == given) return
                // Should the kit refuse, the list is filled anew and shows what holds.
                spinner.tag = values to picked
                chosen(picked)
            }

            override fun onNothingSelected(parent: AdapterView<*>?) = Unit
        }
    }

    private fun onDone(field: EditText, done: () -> Unit) {
        field.setOnEditorActionListener { _, action, _ ->
            if (action == EditorInfo.IME_ACTION_DONE) done()
            // The keyboard goes away as it would without a listener.
            false
        }
    }

    // ---------------------------------------------------------------- Anki

    /**
     * Asks the AnkiConnect at the address for its decks and note types. With
     * [adopt] the address and the key become the kit's, and where nothing was
     * chosen yet the Lapis note type and its deck are.
     */
    private fun connect(adopt: Boolean) {
        val typed = addressField.text.toString().trim()
        val key = keyField.text.toString()
        if (typed.isEmpty()) {
            decks = emptyList()
            noteTypes = emptyList()
            anki = Check(IDLE)
            if (adopt) change("settings.set", JSONObject().put("anki", JSONObject().put("address", ""))) else show()
            return
        }
        anki = Check(WORKING)
        show()
        val context = applicationContext
        kit({
            val params = JSONObject().put("address", typed).put("adopt", adopt)
            if (key.isNotEmpty()) params.put("key", key)
            val answer = MiningKit.call("anki.check", params) as? JSONObject ?: JSONObject()
            // The kit has the key from here on; the device keeps it for the next start.
            val kept = !adopt || key.isEmpty() || Secrets.set(context, Secrets.ANKI_KEY, key)
            Triple(answer, kept, askCards())
        }) { result, failure ->
            if (result == null) {
                anki = Check(BAD, failure.orEmpty())
                show()
                refreshCards()
                return@kit
            }
            val (answer, kept, cards) = result
            if (adopt && key.isNotEmpty()) keyField.setText("")
            problem = if (kept) null else getString(R.string.kit_not_kept)
            decks = strings(answer.optJSONArray("decks"))
            noteTypes = strings(answer.optJSONArray("noteTypes"))
            answer.optJSONObject("settings")?.let { take(it, typed = adopt) }
            anki = Check(GOOD, getString(if (answer.optJSONObject("lapis") != null) R.string.kit_connected_lapis
                                         else R.string.kit_connected_plain))
            if (cards != null) cardProblem = cards
            show()
        }
    }

    private fun strings(list: JSONArray?): List<String> =
        if (list == null) emptyList() else (0 until list.length()).map { list.optString(it) }

    /** Forgets the AnkiConnect's key or the audio source's address, in the kit and on the device. */
    private fun forget(secret: String) {
        kit({ MiningKit.setSecret(secret, "") }) { _, failure ->
            problem = failure
            if (failure == null && secret == Secrets.ANKI_KEY) {
                keyField.setText("")
                keySet = false
            } else if (failure == null) {
                audioField.setText("")
                audioSet = false
                audio = Check(IDLE)
            }
            show()
            refreshCards()
        }
    }

    /** Tells the kit the tags in the text field, when they are not the ones it holds. */
    private fun setTags() {
        val wanted = tagsField.text.toString().split(Regex("\\s+")).filter { it.isNotEmpty() }
        if (wanted.joinToString(" ") == keptTags) return
        change("settings.set", JSONObject().put("anki", JSONObject().put("tags", JSONArray(wanted)))) {
            if (!tagsField.hasFocus()) tagsField.setText(keptTags)
        }
    }

    /** What each field of the chosen note type gets when a card is made. */
    private fun showFields() {
        val pad = (20 * resources.displayMetrics.density).toInt()
        val column = LinearLayout(this)
        column.orientation = LinearLayout.VERTICAL
        column.setPadding(pad, pad / 2, pad, 0)
        val note = TextView(this, null, 0, R.style.Note)
        note.setText(R.string.kit_fields_note)
        column.addView(note)
        val spinners = mutableListOf<Pair<String, Spinner>>()
        for ((field, role) in fields) {
            val label = TextView(this, null, 0, R.style.SettingLabel)
            label.text = field
            column.addView(label)
            val spinner = Spinner(this)
            spinner.minimumHeight = (48 * resources.displayMetrics.density).toInt()
            label.labelFor = View.generateViewId().also { spinner.id = it }
            column.addView(spinner, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                           ViewGroup.LayoutParams.WRAP_CONTENT))
            spinners.add(field to spinner)
            markRole(field, spinner, role)
        }
        val legend = TextView(this, null, 0, R.style.CardTitle)
        legend.setText(R.string.kit_fields_legend)
        legend.setPadding(0, pad, 0, 0)
        column.addView(legend)
        for (role in roles.filter { it.role != "none" }) {
            val line = TextView(this, null, 0, R.style.Note)
            line.text = getString(R.string.kit_role_line, role.label, role.detail)
            column.addView(line)
        }
        val scroll = ScrollView(this)
        scroll.addView(column)
        fieldSpinners = spinners
        AlertDialog.Builder(this).setTitle(getString(R.string.kit_fields_title, noteType)).setView(scroll)
            .setPositiveButton(R.string.kit_done, null)
            .setOnDismissListener { fieldSpinners = emptyList() }
            .show()
    }

    /** Shows what a field gets, in its list in the fields dialog. */
    private fun markRole(field: String, spinner: Spinner, role: String?) {
        val known = roles.map { it.role }
        val current = role ?: "none"
        val values = if (current in known) known else known + current
        fill(spinner, values, values.map { value -> roles.firstOrNull { it.role == value }?.label ?: value }, current) {
            change("anki.setRole", JSONObject().put("field", field).put("role", it))
        }
    }

    // ---------------------------------------------------------------- dictionaries

    private fun showDictionaries() {
        if (dictionaries != shownDictionaries) {
            shownDictionaries = dictionaries
            dictionaryRows.removeAllViews()
            for ((index, dictionary) in dictionaries.withIndex()) {
                val row = layoutInflater.inflate(R.layout.mining_dictionary, dictionaryRows, false)
                val enabled = row.findViewById<Switch>(R.id.enabled)
                enabled.text = dictionary.title
                enabled.isChecked = dictionary.enabled
                enabled.setOnCheckedChangeListener { _, on ->
                    if (on != dictionary.enabled) {
                        dictionaryCall("dict.enable", JSONObject().put("id", dictionary.id).put("enabled", on))
                    }
                }
                row.findViewById<TextView>(R.id.holds).text = holds(dictionary)
                val up = row.findViewById<Button>(R.id.up)
                val down = row.findViewById<Button>(R.id.down)
                up.isEnabled = index > 0
                down.isEnabled = index < dictionaries.size - 1
                up.setOnClickListener { move(index, index - 1) }
                down.setOnClickListener { move(index, index + 1) }
                row.findViewById<Button>(R.id.remove).setOnClickListener {
                    AlertDialog.Builder(this)
                        .setMessage(getString(R.string.kit_remove_question, dictionary.title))
                        .setNegativeButton(R.string.cancel, null)
                        .setPositiveButton(R.string.remove) { _, _ ->
                            dictionaryCall("dict.remove", JSONObject().put("id", dictionary.id))
                        }
                        .show()
                }
                dictionaryRows.addView(row)
            }
        }
        val reading = copying
        val busy = importRunning || reading != null
        importStateText.visibility = if (busy) View.VISIBLE else View.GONE
        importProgressBar.visibility = if (busy) View.VISIBLE else View.GONE
        importStop.visibility = if (importRunning) View.VISIBLE else View.GONE
        importFile.visibility = if (busy) View.GONE else View.VISIBLE
        importAddress.visibility = if (busy) View.GONE else View.VISIBLE
        importStateText.text = when {
            reading != null -> getString(R.string.kit_copying, reading)
            importState == "downloading" -> getString(R.string.kit_downloading)
            importTitle.isEmpty() -> getString(R.string.kit_importing)
            else -> getString(R.string.kit_importing_named, importTitle)
        }
        importProgressBar.isIndeterminate = reading != null
        importProgressBar.progress = (importProgress.coerceIn(0.0, 1.0) * 1000).toInt()
        val failed = importState == "failed" && importError.isNotEmpty() && !busy
        importErrorText.text = importError
        importErrorText.visibility = if (failed) View.VISIBLE else View.GONE
    }

    /** What a dictionary holds, in a few words. */
    private fun holds(dictionary: Installed): String {
        val numbers = NumberFormat.getIntegerInstance()
        fun counted(count: Int, words: Int) =
            if (count > 0) resources.getQuantityString(words, count, numbers.format(count)) else null
        val parts = listOfNotNull(counted(dictionary.terms, R.plurals.kit_words),
                                  counted(dictionary.frequencies, R.plurals.kit_frequencies),
                                  counted(dictionary.pitches, R.plurals.kit_pitches))
        return if (parts.isEmpty()) getString(R.string.kit_empty) else parts.joinToString(", ")
    }

    /** A call that answers with the dictionaries and how far an import is. */
    private fun dictionaryCall(method: String, params: JSONObject? = null) {
        kit({ (MiningKit.call(method, params) as? JSONObject) to askCards() }) { answer, failure ->
            problem = failure
            answer?.first?.let { takeDictionaries(it) }
            answer?.second?.let { cardProblem = it }
            if (failure != null) shownDictionaries = null
            show()
            if (importRunning) watchImport()
        }
    }

    /** The order is the order the popup shows a word's meanings in. */
    private fun move(from: Int, to: Int) {
        if (from !in dictionaries.indices || to !in dictionaries.indices) return
        val moved = dictionaries.toMutableList()
        moved.add(to, moved.removeAt(from))
        val order = JSONArray(moved.map { it.id })
        val disabled = JSONArray(moved.filter { !it.enabled }.map { it.id })
        kit({ MiningKit.call("dict.arrange", JSONObject().put("order", order).put("disabled", disabled)) as? JSONObject }) {
                answer, failure ->
            problem = failure
            answer?.let { takeDictionaries(it) }
            show()
            if (failure != null) dictionaryCall("dict.list")
        }
    }

    /** Imports a dictionary picked in the system's file picker. The kit reads a copy: the picked file is only lent. */
    private fun importPicked(file: Uri) {
        val name = nameOf(file)
        copying = name
        show()
        val copy = File(cacheDir, COPY_PREFIX + UUID.randomUUID() + ".zip")
        val unreadable = getString(R.string.kit_unreadable, name)
        kit({
            val read = try {
                contentResolver.openInputStream(file)?.use { from -> copy.outputStream().use { from.copyTo(it) } } != null
            } catch (e: Exception) {
                false
            }
            if (!read) {
                copy.delete()
                throw KitError(unreadable, "internal")
            }
            try {
                MiningKit.call("dict.import", JSONObject().put("path", copy.path).put("name", name).put("remove", true))
                    as? JSONObject
            } catch (e: KitError) {
                copy.delete()
                throw e
            }
        }) { answer, failure ->
            copying = null
            problem = failure
            answer?.let { takeDictionaries(it) }
            show()
            if (importRunning) watchImport()
        }
    }

    private fun nameOf(file: Uri): String {
        val named = try {
            contentResolver.query(file, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        } catch (e: Exception) {
            null
        }
        return named?.takeIf { it.isNotBlank() } ?: file.lastPathSegment ?: "dictionary.zip"
    }

    private fun removeCopies() {
        cacheDir.listFiles { _, name -> name.startsWith(COPY_PREFIX) }?.forEach { it.delete() }
    }

    private fun askAddress() {
        val pad = (20 * resources.displayMetrics.density).toInt()
        val field = EditText(this)
        field.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
        field.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_ACTION_DONE
        field.setHint(R.string.kit_download_hint)
        field.importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO
        val holder = LinearLayout(this)
        holder.setPadding(pad, 0, pad, 0)
        holder.addView(field, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                        ViewGroup.LayoutParams.WRAP_CONTENT))
        AlertDialog.Builder(this).setTitle(R.string.kit_download_title).setMessage(R.string.kit_download_note)
            .setView(holder)
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.kit_download) { _, _ ->
                dictionaryCall("dict.download", JSONObject().put("url", field.text.toString().trim()))
            }
            .show()
    }

    /** Follows an import until it is over. */
    private fun watchImport() {
        if (watching) return
        watching = true
        main.postDelayed(::lookAtImport, WATCH_MS)
    }

    private fun lookAtImport() {
        kit({ MiningKit.call("dict.list") as? JSONObject }) { answer, _ ->
            answer?.let { takeDictionaries(it) }
            show()
            if (answer != null && importRunning) {
                main.postDelayed(::lookAtImport, WATCH_MS)
            } else {
                watching = false
                refreshCards()
            }
        }
    }

    // ---------------------------------------------------------------- word audio

    /** Asks the audio source for the sounds of a word every source has, and keeps an address that answers. */
    private fun checkAudio() {
        val typed = audioField.text.toString().trim()
        if (typed.isEmpty() && !audioSet) return
        audio = Check(WORKING)
        show()
        kit({
            val answer = MiningKit.call("audio.check", if (typed.isEmpty()) null else JSONObject().put("address", typed))
                as? JSONObject
            val count = answer?.optJSONArray("names")?.length() ?: 0
            val kept = typed.isEmpty() || MiningKit.setSecret(Secrets.AUDIO_ADDRESS, typed)
            Triple(count, kept, askCards())
        }) { result, failure ->
            if (result == null) {
                audio = Check(BAD, failure.orEmpty())
            } else {
                val (count, kept, cards) = result
                if (typed.isNotEmpty()) {
                    audioField.setText("")
                    audioSet = true
                }
                problem = if (kept) null else getString(R.string.kit_not_kept)
                audio = Check(GOOD, resources.getQuantityString(R.plurals.kit_audio_answers, count, count))
                if (cards != null) cardProblem = cards
            }
            show()
        }
    }

    companion object {
        private const val REQUEST_DICTIONARY = 1
        private const val WATCH_MS = 300L
        private const val COPY_PREFIX = "ore-dictionary-"
        private const val IDLE = 0
        private const val WORKING = 1
        private const val GOOD = 2
        private const val BAD = 3
        private val TEXT_SIZES = listOf(70, 80, 90, 100, 115, 130)

        /**
         * The launcher's line about all this, and whether words are looked
         * up. Asks the kit, which must be open: not for the main thread.
         */
        fun summary(context: Context): Pair<String, Boolean> {
            return try {
                val settings = MiningKit.call("settings.get") as? JSONObject ?: return "" to false
                if (!settings.optBoolean("direct")) return context.getString(R.string.kit_summary_off) to false
                val list = (MiningKit.call("dict.list") as? JSONObject)?.optJSONArray("dictionaries") ?: JSONArray()
                val count = (0 until list.length()).count { list.optJSONObject(it)?.optBoolean("enabled", true) == true }
                if (count == 0) return context.getString(R.string.kit_summary_no_dictionary) to true
                val words = context.resources.getQuantityString(R.plurals.kit_dictionaries, count, count)
                val cards = (MiningKit.call("page.init") as? JSONObject)?.optJSONObject("cards")
                val line = if (cards?.optBoolean("possible") == true) {
                    context.getString(R.string.kit_summary_cards, words,
                                      settings.optJSONObject("anki")?.optString("deck").orEmpty())
                } else {
                    context.getString(R.string.kit_summary_no_cards, words, cards?.optString("problem").orEmpty())
                }
                line to true
            } catch (e: KitError) {
                (e.message ?: "") to false
            }
        }
    }
}

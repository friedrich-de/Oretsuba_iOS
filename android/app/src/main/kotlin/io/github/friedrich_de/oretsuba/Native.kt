package io.github.friedrich_de.oretsuba

import android.view.Surface

/**
 * What a running game asks of the screen that shows it. Both are called on
 * the engine's thread; texts are UTF-8.
 */
interface EngineHost {
    /** A message box: kind 0 = OK, 1 = Yes / No. Answer with [Native.answerDialog]. */
    fun onDialog(text: ByteArray, kind: Int)

    /** A line of text to type (a save's comment). Answer with [Native.answerTextInput]. */
    fun onTextInput(initial: ByteArray)
}

/**
 * The app's native library: the engine and its glue (src/main/cpp). A session
 * is one game. Texts and paths cross as UTF-8 bytes, because JNI's own strings
 * are not UTF-8 for every character.
 *
 * Everything but [newSession], [setSurface], [miningCheck] and the calls of the
 * mining kit that take no session belongs to the engine's thread.
 */
object Native {
    init {
        System.loadLibrary("oretsuba")
    }

    @JvmStatic external fun newSession(host: EngineHost): Long

    /** Starts the game. Null when it runs, otherwise the reason it does not. */
    @JvmStatic external fun boot(session: Long, dataDir: ByteArray, saveDir: ByteArray, autosaveLines: Int,
                                 resume: Boolean): ByteArray?

    @JvmStatic external fun destroy(session: Long)

    /** One frame: 0 to go on, 1 when the game asked to quit, -1 on a fatal error ([lastError]). */
    @JvmStatic external fun frame(session: Long): Int

    @JvmStatic external fun lastError(session: Long): ByteArray

    /** The app left the screen or came back: the game and its sound stop and go on. */
    @JvmStatic external fun setPaused(session: Long, paused: Boolean)

    /** The surface the picture goes to, or null when it is gone. From the main thread. */
    @JvmStatic external fun setSurface(session: Long, surface: Surface?)

    /** Draws the picture if it changed since the last call. */
    @JvmStatic external fun present(session: Long): Boolean

    /** Input in game coordinates (0..799, 0..599); buttons 0 left, 1 right, 2 middle. */
    @JvmStatic external fun mouseMove(session: Long, x: Int, y: Int)

    @JvmStatic external fun mouseButton(session: Long, button: Int, down: Boolean)

    /** +120 = one notch away from the player. */
    @JvmStatic external fun mouseWheel(session: Long, delta: Int)

    /** Windows virtual-key codes. */
    @JvmStatic external fun key(session: Long, vk: Int, down: Boolean)

    @JvmStatic external fun gameCommand(session: Long, command: Int)

    /** Whether one of the game's films plays at the moment. */
    @JvmStatic external fun moviePlaying(session: Long): Boolean

    @JvmStatic external fun answerDialog(session: Long, result: Int)

    @JvmStatic external fun answerTextInput(session: Long, ok: Boolean, text: ByteArray?)

    /**
     * Asks the server named in the game folder's mining.txt whether it takes
     * the key. state[0]: 1 lines will arrive, 0 no mining.txt, -1 something is
     * wrong; the answer is a sentence for the player. Waits for the server:
     * call it away from the main thread.
     */
    @JvmStatic external fun miningCheck(dataDir: ByteArray, state: IntArray): ByteArray

    // Mining in the game (docs/MINING.md). The app speaks to the kit through MiningKit.kt.

    /** Opens the app's one mining kit. 0 with the reason in error[0] when it cannot be opened. */
    @JvmStatic external fun kitOpen(folder: ByteArray, resources: ByteArray, error: Array<ByteArray?>): Long

    /** The kit's answer to a method, as JSON. Waits for it, which may take the network's time. */
    @JvmStatic external fun kitCall(kit: Long, method: ByteArray, params: ByteArray): ByteArray

    /** A file of the kit's for the popup's page, with its media type in type[0]. Null when there is none. */
    @JvmStatic external fun kitData(kit: Long, method: ByteArray, params: ByteArray, type: Array<ByteArray?>): ByteArray?

    /** Whether a tap on the text looks a word up. */
    @JvmStatic external fun kitDirect(kit: Long): Boolean

    /** Gives the game the kit, or takes it away with 0. */
    @JvmStatic external fun setKit(session: Long, kit: Long)

    /**
     * What a tap at a place of the game's screen is, into what[0]: 0 the game's,
     * 1 a word of the text, 2 the text box beside the words. For a word the
     * answer is the JSON the popup's page takes.
     */
    @JvmStatic external fun textTap(session: Long, x: Int, y: Int, what: IntArray): ByteArray?

    /** While the popup is open the game stands still. */
    @JvmStatic external fun setReadingHold(session: Long, hold: Boolean)

    /** From [KitHttp]: how far a request is. False when the kit has given the request up. */
    @JvmStatic external fun fetchStep(handle: Long, taken: Long, expected: Long): Boolean

    // Called by native code, from any of its threads.

    @JvmStatic
    fun kitFetch(method: ByteArray, url: ByteArray, headers: Array<ByteArray>, body: ByteArray?, timeoutMs: Int,
                 maxBytes: Long, toFile: ByteArray?, handle: Long): ByteArray =
        KitHttp.fetch(String(method, Charsets.UTF_8), String(url, Charsets.UTF_8), headers, body, timeoutMs, maxBytes,
                      toFile?.let { String(it, Charsets.UTF_8) }, handle)

    /** 16-bit stereo samples as AAC in an MPEG-4 file; null when this device cannot. */
    @JvmStatic
    fun encodeVoice(samples: ByteArray, rate: Int): ByteArray? = VoiceEncoder.encode(samples, rate)

    /** What the mining kit tells without having been asked. */
    @JvmStatic
    fun kitEvent(name: ByteArray, payload: ByteArray) =
        MiningKit.heard(String(name, Charsets.UTF_8), String(payload, Charsets.UTF_8))

    @JvmStatic
    fun renderGlyph(codepoint: Int, serif: Boolean, bold: Boolean, size: Int): ByteArray? =
        Glyphs.render(codepoint, serif, bold, size)

    /** One request to AnkiDroid on this device (AnkiDroid.kt); request and answer are JSON. */
    @JvmStatic
    fun ankiDroid(method: ByteArray, json: ByteArray): ByteArray =
        AnkiDroid.request(String(method, Charsets.UTF_8), String(json, Charsets.UTF_8)).toByteArray(Charsets.UTF_8)

    @JvmStatic
    fun httpSend(method: ByteArray, url: ByteArray, headers: Array<ByteArray>, body: ByteArray?,
                 timeoutMs: Int): ByteArray =
        Http.send(String(method, Charsets.UTF_8), String(url, Charsets.UTF_8), headers, body, timeoutMs)
}

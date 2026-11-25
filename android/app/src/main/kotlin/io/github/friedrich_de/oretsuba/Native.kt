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
 * Everything but [newSession], [setSurface] and [miningCheck] belongs to the
 * engine's thread.
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

    @JvmStatic external fun answerDialog(session: Long, result: Int)

    @JvmStatic external fun answerTextInput(session: Long, ok: Boolean, text: ByteArray?)

    /**
     * Asks the server named in the game folder's mining.txt whether it takes
     * the key. state[0]: 1 lines will arrive, 0 no mining.txt, -1 something is
     * wrong; the answer is a sentence for the player. Waits for the server:
     * call it away from the main thread.
     */
    @JvmStatic external fun miningCheck(dataDir: ByteArray, state: IntArray): ByteArray

    // Called by native code, from any of its threads.

    @JvmStatic
    fun renderGlyph(codepoint: Int, serif: Boolean, bold: Boolean, size: Int): ByteArray? =
        Glyphs.render(codepoint, serif, bold, size)

    @JvmStatic
    fun httpSend(method: ByteArray, url: ByteArray, headers: Array<ByteArray>, body: ByteArray?,
                 timeoutMs: Int): ByteArray =
        Http.send(String(method, Charsets.UTF_8), String(url, Charsets.UTF_8), headers, body, timeoutMs)
}

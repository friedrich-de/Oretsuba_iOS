package io.github.friedrich_de.oretsuba

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.util.Log
import java.io.File
import java.security.KeyStore
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import org.json.JSONException
import org.json.JSONObject

/** What the kit answered in place of a result: a sentence for the player, and its kind. */
internal class KitError(message: String, val kind: String) : Exception(message)

/**
 * The mining kit for the app (docs/MINING.md): looking words up and making
 * Anki cards inside the game. There is one for the app's whole life; the
 * launcher's settings and the popup over the game both speak to it. The kit
 * itself is C++ (engine/src/mining); this carries its calls and keeps the two
 * secrets that are not among the kit's settings ([Secrets]).
 */
internal object MiningKit {
    private const val NO_ANSWER = """{"ok":false,"error":"The kit did not answer.","kind":"internal"}"""

    @Volatile
    private var handle = 0L

    /** The app itself, which outlives every screen. */
    @Volatile
    private var appContext: Context? = null

    /** Why the kit could not be opened; empty when it is open or was not tried yet. */
    @Volatile
    var problem = ""
        private set

    private val listeners = CopyOnWriteArrayList<(String, String) -> Unit>()

    /** Calls may wait for the network, so they run here, several at a time. */
    val work: ExecutorService = Executors.newCachedThreadPool { task -> Thread(task, "mining-kit") }

    /** The app itself, once the kit was opened. */
    val context: Context? get() = appContext

    /** What native code takes for the kit (Native.setKit); 0 when it is not open. */
    val kit: Long get() = handle

    /**
     * Opens the kit when it is not open yet. False when it cannot be opened;
     * [problem] then says why. Reads a few small files: fine on the main thread.
     */
    @Synchronized
    fun open(context: Context): Boolean {
        if (handle != 0L) return true
        val application = context.applicationContext
        appContext = application
        val resources = try {
            resources(application)
        } catch (e: Exception) {
            problem = "The app's mining files are missing."
            return false
        }
        val folder = File(application.filesDir, "mining")
        folder.mkdirs()
        val error = arrayOfNulls<ByteArray>(1)
        val opened = Native.kitOpen(folder.path.toByteArray(), resources.path.toByteArray(), error)
        if (opened == 0L) {
            problem = error[0]?.let { String(it, Charsets.UTF_8) } ?: "Mining in the game could not be started."
            return false
        }
        handle = opened
        problem = ""
        val secrets = JSONObject()
        secrets.put("ankiKey", Secrets.get(application, Secrets.ANKI_KEY))
        secrets.put("audioAddress", Secrets.get(application, Secrets.AUDIO_ADDRESS))
        callRaw("secrets.set", secrets.toString())
        return true
    }

    /**
     * The kit reads its rules for undoing inflections from a file, and what
     * the package carries is no file: it is copied out, anew with every start
     * so that an update of the app brings its own.
     */
    private fun resources(context: Context): File {
        val folder = File(context.filesDir, "mining-files")
        folder.mkdirs()
        val fresh = File(folder, "transforms.json.new")
        context.assets.open("mining/transforms.json").use { from -> fresh.outputStream().use { from.copyTo(it) } }
        if (!fresh.renameTo(File(folder, "transforms.json"))) throw IllegalStateException("transforms.json")
        return folder
    }

    /** Whether a tap on the text looks a word up. */
    val direct: Boolean get() = handle != 0L && Native.kitDirect(handle)

    /** The kit's answer to a method as JSON text. Waits for the answer: not for the main thread. */
    fun callRaw(method: String, params: String): String {
        val kit = handle
        if (kit == 0L) return NO_ANSWER
        return String(Native.kitCall(kit, method.toByteArray(), params.toByteArray()), Charsets.UTF_8)
    }

    /** The result of a method, or the kit's sentence as a [KitError]. Waits like [callRaw]. */
    fun call(method: String, params: JSONObject? = null): Any? {
        val answer = try {
            JSONObject(callRaw(method, params?.toString() ?: "{}"))
        } catch (e: JSONException) {
            throw KitError("The kit did not answer.", "internal")
        }
        if (!answer.optBoolean("ok")) {
            throw KitError(answer.optString("error", "The kit did not answer."), answer.optString("kind", "internal"))
        }
        return answer.opt("result")
    }

    /** A file of the kit's for the popup's page and its media type. Waits like a call. */
    fun data(method: String, params: String): Pair<ByteArray, String>? {
        val kit = handle
        if (kit == 0L) return null
        val type = arrayOfNulls<ByteArray>(1)
        val bytes = Native.kitData(kit, method.toByteArray(), params.toByteArray(), type) ?: return null
        return bytes to (type[0]?.let { String(it, Charsets.UTF_8) } ?: "application/octet-stream")
    }

    /** Tells the kit a secret and keeps it. False when it could not be kept: the kit then has it until the app ends. */
    fun setSecret(name: String, value: String): Boolean {
        val kitName = if (name == Secrets.ANKI_KEY) "ankiKey" else "audioAddress"
        call("secrets.set", JSONObject().put(kitName, value))
        val context = appContext ?: return false
        return Secrets.set(context, name, value)
    }

    /** What the kit tells without having been asked reaches [listener], on whatever thread the kit is on. */
    fun listen(listener: (name: String, payload: String) -> Unit) {
        listeners.add(listener)
    }

    fun unlisten(listener: (name: String, payload: String) -> Unit) {
        listeners.remove(listener)
    }

    /** From native code (Native.kitEvent). */
    fun heard(name: String, payload: String) {
        for (listener in listeners) listener(name, payload)
    }
}

/**
 * The two things of the kit's that are secrets: an AnkiConnect's key, and the
 * audio source's address, which often holds a key of its own. They are kept
 * in the app's own settings, sealed with a key that this device's Keystore
 * holds and never gives out.
 */
internal object Secrets {
    const val ANKI_KEY = "anki-key"
    const val AUDIO_ADDRESS = "audio-address"

    private const val STORE = "AndroidKeyStore"
    private const val ALIAS = "oretsuba.mining"
    private const val FILE = "mining-secrets"
    private const val SEAL = "AES/GCM/NoPadding"
    private const val NONCE = 12

    private fun key(): SecretKey {
        val store = KeyStore.getInstance(STORE)
        store.load(null)
        (store.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val maker = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, STORE)
        maker.init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build())
        return maker.generateKey()
    }

    /** The secret, or an empty text when there is none or it can no longer be read. */
    fun get(context: Context, name: String): String {
        val kept = context.getSharedPreferences(FILE, Context.MODE_PRIVATE).getString(name, null) ?: return ""
        return try {
            val bytes = Base64.decode(kept, Base64.NO_WRAP)
            val cipher = Cipher.getInstance(SEAL)
            cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, bytes, 0, NONCE))
            String(cipher.doFinal(bytes, NONCE, bytes.size - NONCE), Charsets.UTF_8)
        } catch (e: Exception) {
            Log.w("Oretsuba", "mining: a kept secret can no longer be read: $e")
            ""
        }
    }

    /** Keeps a secret, or forgets it when it is empty. False when it could not be kept. */
    fun set(context: Context, name: String, value: String): Boolean {
        val settings = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)
        if (value.isEmpty()) {
            settings.edit().remove(name).apply()
            return true
        }
        return try {
            val cipher = Cipher.getInstance(SEAL)
            cipher.init(Cipher.ENCRYPT_MODE, key())
            val sealed = cipher.iv + cipher.doFinal(value.toByteArray(Charsets.UTF_8))
            settings.edit().putString(name, Base64.encodeToString(sealed, Base64.NO_WRAP)).apply()
            true
        } catch (e: Exception) {
            Log.w("Oretsuba", "mining: a secret could not be kept: $e")
            false
        }
    }
}

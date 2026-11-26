package io.github.friedrich_de.oretsuba

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.net.ConnectException
import java.net.HttpURLConnection
import java.net.MalformedURLException
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.net.ssl.SSLException

/**
 * HTTP for the mining kit (docs/MINING.md): what it asks of an AnkiConnect, a
 * word's sound and a dictionary to download. Unlike [Http] it gives the whole
 * answer, its type and where it came from, writes a download into a file and
 * can be given up while it runs. One request, on the calling thread; nothing
 * is kept between requests.
 */
internal object KitHttp {
    private const val HOPS = 5

    private const val GIVEN_UP = "The request was given up."
    private const val LOOK_MS = 150L

    private class Answer {
        var status = 0
        var tooLarge = false
        var type = ""
        var from = ""
        var error = ""
        var body = ByteArray(0)
    }

    /**
     * Looks, beside a request, whether the kit has given it up, and then
     * closes its connection: a request that waits for a server that does not
     * answer would otherwise only hear of it when its time is over.
     */
    private class Watch(private val handle: Long) {
        @Volatile
        var connection: HttpURLConnection? = null

        @Volatile
        var givenUp = false
            private set

        private val thread = Thread({
            try {
                while (true) {
                    Thread.sleep(LOOK_MS)
                    // Without a length the kit's progress stays as it is; only the question is asked.
                    if (!Native.fetchStep(handle, 0, 0)) {
                        givenUp = true
                        connection?.disconnect()
                        return@Thread
                    }
                }
            } catch (e: InterruptedException) {
                // The request is over.
            }
        }, "mining-kit-watch")

        fun start() {
            thread.isDaemon = true
            thread.start()
        }

        /** Returns once the watcher has ended: [handle] is the request's own and goes with it. */
        fun end() {
            thread.interrupt()
            thread.join()
        }
    }

    /**
     * The answer as native code reads it (jni_kit_fetch): the status and a 1
     * for an answer longer than [maxBytes], both little-endian 32-bit numbers;
     * the answer's type, where it came from and why none came, each behind
     * its length; then the body. [headers] holds names and values in turn.
     * With [toFile] a good answer's body goes into that file. [handle] is
     * what [Native.fetchStep] takes.
     */
    fun fetch(method: String, url: String, headers: Array<ByteArray>, body: ByteArray?, timeoutMs: Int, maxBytes: Long,
              toFile: String?, handle: Long): ByteArray {
        val answer = Answer()
        var whole = false
        val watch = Watch(handle)
        watch.start()
        try {
            whole = run(method, url, headers, body, maxOf(1000, timeoutMs), maxBytes, toFile, handle, answer, watch)
        } catch (e: MalformedURLException) {
            answer.error = "The address is not one."
        } catch (e: UnknownHostException) {
            answer.error = "No server was found under that name."
        } catch (e: SocketTimeoutException) {
            answer.error = "The server did not answer in time."
        } catch (e: ConnectException) {
            answer.error = "The server does not take a connection."
        } catch (e: SSLException) {
            answer.error = "No secure connection to the server: " + (e.localizedMessage ?: e.javaClass.simpleName)
        } catch (e: Exception) {
            answer.error = e.localizedMessage ?: e.javaClass.simpleName
        } finally {
            watch.end()
        }
        // A connection closed under the request fails in some way of its own; why is known here.
        if (watch.givenUp) {
            answer.error = GIVEN_UP
            whole = false
        }
        if (answer.error.isNotEmpty()) answer.status = 0
        if (!whole && toFile != null) File(toFile).delete()
        return pack(answer)
    }

    /** True when the answer arrived whole. */
    private fun run(method: String, url: String, headers: Array<ByteArray>, body: ByteArray?, timeout: Int,
                    maxBytes: Long, toFile: String?, handle: Long, answer: Answer, watch: Watch): Boolean {
        var address = URL(url)
        var verb = method.ifEmpty { "GET" }
        var sent = body
        var hops = 0
        while (true) {
            if (!Http.mayGo(address)) {
                answer.error = Http.PLAIN_REFUSED
                return false
            }
            val connection = address.openConnection() as? HttpURLConnection
            if (connection == null) {
                answer.error = "The address is not one of a web server."
                return false
            }
            watch.connection = connection
            if (watch.givenUp) {
                connection.disconnect()
                return false
            }
            try {
                connection.requestMethod = verb
                connection.connectTimeout = timeout
                connection.readTimeout = timeout
                connection.useCaches = false
                // Followed here, so that the answer can say where it came from.
                connection.instanceFollowRedirects = false
                for (i in 0 until headers.size / 2) {
                    connection.setRequestProperty(String(headers[i * 2], Charsets.UTF_8),
                                                  String(headers[i * 2 + 1], Charsets.UTF_8))
                }
                if (sent != null) {
                    connection.doOutput = true
                    connection.setFixedLengthStreamingMode(sent.size)
                    connection.outputStream.use { it.write(sent) }
                }
                val status = connection.responseCode
                val onward = if (status in REDIRECTS) connection.getHeaderField("Location") else null
                if (onward != null) {
                    if (++hops > HOPS) {
                        answer.error = "The server sends the request round in circles."
                        return false
                    }
                    val next = URL(address, onward)
                    if (address.protocol == "https" && next.protocol != "https") {
                        answer.error = "The server sends the request on to an address without https."
                        return false
                    }
                    if (status != 307 && status != 308 && verb != "GET" && verb != "HEAD") {
                        verb = "GET"
                        sent = null
                    }
                    address = next
                    continue
                }
                answer.status = status
                answer.type = connection.contentType?.substringBefore(';')?.trim()?.lowercase().orEmpty()
                answer.from = address.toString()
                val expected = connection.contentLengthLong
                val stream = (if (status >= 400) connection.errorStream else connection.inputStream) ?: return true
                val toDisk = toFile != null && status in 200..299
                val kept = ByteArrayOutputStream()
                val out: OutputStream = if (toDisk) FileOutputStream(toFile) else kept
                var taken = 0L
                stream.use { input ->
                    out.use { output ->
                        val buffer = ByteArray(64 * 1024)
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            taken += n
                            if (taken > maxBytes) {
                                answer.tooLarge = true
                                return false
                            }
                            output.write(buffer, 0, n)
                            if (!Native.fetchStep(handle, taken, expected)) {
                                answer.error = GIVEN_UP
                                return false
                            }
                        }
                    }
                }
                if (!toDisk) answer.body = kept.toByteArray()
                return true
            } finally {
                connection.disconnect()
            }
        }
    }

    private val REDIRECTS = setOf(301, 302, 303, 307, 308)

    private fun pack(answer: Answer): ByteArray {
        val type = answer.type.toByteArray(Charsets.UTF_8)
        val from = answer.from.toByteArray(Charsets.UTF_8)
        val error = answer.error.toByteArray(Charsets.UTF_8)
        val out = ByteBuffer.allocate(20 + type.size + from.size + error.size + answer.body.size)
            .order(ByteOrder.LITTLE_ENDIAN)
        out.putInt(answer.status).putInt(if (answer.tooLarge) 1 else 0)
        out.putInt(type.size).put(type).putInt(from.size).put(from).putInt(error.size).put(error)
        out.put(answer.body)
        return out.array()
    }
}

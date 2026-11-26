package io.github.friedrich_de.oretsuba

import java.io.InputStream
import java.net.HttpURLConnection
import java.net.MalformedURLException
import java.net.URL
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * HTTP for mining (docs/MINING.md): one request, sent on the calling thread.
 * Nothing is kept between requests: no cookies, no cache.
 */
internal object Http {
    private const val ANSWER_LIMIT = 4096

    /** What a request hears that would go into the internet without https. */
    const val PLAIN_REFUSED =
        "An address in the internet has to begin with https://. Plain http:// is for a server in your own network."

    private val IPV4 = Regex("\\d{1,3}(\\.\\d{1,3}){3}")

    /**
     * Whether a request may go to [address] as it is. These requests carry
     * keys, so plain http is only for a server in the player's own network:
     * one given by its IP address, by a name without dots or by one ending
     * in .local. That is the line iOS draws for an app by itself; Android
     * lets an app draw it.
     */
    fun mayGo(address: URL): Boolean {
        if (!address.protocol.equals("http", ignoreCase = true)) return true
        val host = address.host.lowercase().trimEnd('.')
        return ':' in host || IPV4.matches(host) || '.' !in host || host.endsWith(".local")
    }

    /**
     * The status as a little-endian 32-bit number, then the start of the
     * answer's body; or 0 and the reason no answer came. [headers] holds
     * names and values in turn, as UTF-8.
     */
    fun send(method: String, url: String, headers: Array<ByteArray>, body: ByteArray?, timeoutMs: Int): ByteArray {
        var connection: HttpURLConnection? = null
        return try {
            val address = URL(url)
            if (!mayGo(address)) return pack(0, PLAIN_REFUSED.toByteArray())
            connection = address.openConnection() as? HttpURLConnection
                ?: return pack(0, "The address is not one of a web server.".toByteArray())
            val timeout = maxOf(1000, timeoutMs)
            connection.requestMethod = method
            connection.connectTimeout = timeout
            connection.readTimeout = timeout
            connection.useCaches = false
            for (i in 0 until headers.size / 2) {
                connection.setRequestProperty(String(headers[i * 2], Charsets.UTF_8),
                                              String(headers[i * 2 + 1], Charsets.UTF_8))
            }
            if (body != null) {
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(body.size)
                connection.outputStream.use { it.write(body) }
            }
            val status = connection.responseCode
            val stream: InputStream? = if (status >= 400) connection.errorStream else connection.inputStream
            pack(status, stream?.use { start(it) } ?: ByteArray(0))
        } catch (e: MalformedURLException) {
            pack(0, "The address is not one.".toByteArray())
        } catch (e: Exception) {
            pack(0, (e.localizedMessage ?: e.javaClass.simpleName).toByteArray())
        } finally {
            connection?.disconnect()
        }
    }

    private fun start(stream: InputStream): ByteArray {
        val buffer = ByteArray(ANSWER_LIMIT)
        var used = 0
        while (used < buffer.size) {
            val n = stream.read(buffer, used, buffer.size - used)
            if (n < 0) break
            used += n
        }
        return buffer.copyOf(used)
    }

    private fun pack(status: Int, rest: ByteArray): ByteArray =
        ByteBuffer.allocate(4 + rest.size).order(ByteOrder.LITTLE_ENDIAN).putInt(status).put(rest).array()
}

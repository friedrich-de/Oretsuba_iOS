package io.github.friedrich_de.oretsuba

import android.annotation.SuppressLint
import android.app.Activity
import android.graphics.Color
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import android.webkit.RenderProcessGoneDetail
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import java.io.ByteArrayInputStream
import java.io.IOException
import org.json.JSONObject

/**
 * The dictionary popup over the game (docs/MINING.md): a web view that lies
 * over the game's picture and shows nothing but the popup and the marked
 * word. Its page (engine/resources/mining/page) is the same on every system;
 * this is the glue: it serves the page's files, carries the page's calls to
 * the mining kit, and tells the page which character was tapped.
 *
 * While [view] is shown it takes every touch, so nothing else reacts until
 * the popup is closed. Everything here belongs to the main thread.
 *
 * [onClose]: the popup was closed, by the page or because the page broke; let
 * the game go on.
 */
internal class MiningPopup(private val activity: Activity, private val onClose: () -> Unit) {
    /** What the game's screen lays over its picture. It holds the web view, which may have to be made anew. */
    val view = FrameLayout(activity)

    private val main = Handler(Looper.getMainLooper())
    private var web: WebView? = null
    private var ready = false
    private var waiting: String? = null
    private var destroyed = false

    private val events: (String, String) -> Unit = { name, payload ->
        main.post { run("window.oreMining && window.oreMining.event(${JSONObject.quote(name)}, $payload)") }
    }

    init {
        view.visibility = View.INVISIBLE
        MiningKit.listen(events)
        load()
    }

    val isOpen: Boolean get() = view.visibility == View.VISIBLE

    // The page is the app's own, served from the package, and reaches nothing outside it.
    @SuppressLint("SetJavaScriptEnabled")
    private fun load() {
        val made = WebView(activity)
        made.setBackgroundColor(Color.TRANSPARENT)
        made.overScrollMode = View.OVER_SCROLL_NEVER
        made.isVerticalScrollBarEnabled = false
        made.isHorizontalScrollBarEnabled = false
        made.settings.javaScriptEnabled = true
        made.settings.allowFileAccess = false
        made.settings.allowContentAccess = false
        // The popup has its own setting for the size of its text.
        made.settings.textZoom = 100
        // A word's sound plays after it was fetched, which is no longer "during" the tap.
        made.settings.mediaPlaybackRequiresUserGesture = false
        made.addJavascriptInterface(Bridge(), "oreAndroid")
        made.webViewClient = Pages()
        view.addView(made, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                    ViewGroup.LayoutParams.MATCH_PARENT))
        web = made
        made.loadUrl(PAGE)
    }

    // ---------------------------------------------------------------- what the game tells the page

    /**
     * Shows the popup for a tapped character. [tap] is the engine's JSON
     * (Native.textTap); [picture] is where the game's picture is in [view],
     * in the screen's pixels.
     */
    fun show(tap: String, picture: RectF) {
        if (destroyed) return
        // The page counts in CSS pixels, of which the screen has `density` real ones each.
        val density = activity.resources.displayMetrics.density
        val place = "[${picture.left / density},${picture.top / density}," +
            "${picture.width() / density},${picture.height() / density}]"
        val script = "window.oreMining.show(Object.assign($tap, {picture: $place}))"
        view.visibility = View.VISIBLE
        if (ready) run(script) else waiting = script
    }

    /** Closes the popup from the app's side, as the page's own close does. */
    fun close() {
        if (!isOpen) return
        waiting = null
        view.visibility = View.INVISIBLE
        run("window.oreMining && window.oreMining.hide()")
        onClose()
    }

    fun destroy() {
        destroyed = true
        MiningKit.unlisten(events)
        main.removeCallbacksAndMessages(null)
        view.removeAllViews()
        web?.destroy()
        web = null
    }

    private fun run(script: String) {
        if (!destroyed) web?.evaluateJavascript(script, null)
    }

    // ---------------------------------------------------------------- the page's calls

    /** What the page's script (HOST_SCRIPT) reaches as `oreAndroid`. Its calls come on a thread of the web view's. */
    private inner class Bridge {
        @JavascriptInterface
        fun post(id: Int, method: String, params: String) {
            main.post { called(id, method, params) }
        }
    }

    private fun called(id: Int, method: String, params: String) {
        if (destroyed) return
        // What starts with "popup." is for the app itself.
        if (method.startsWith("popup.")) {
            if (method == "popup.ready") {
                ready = true
                waiting?.let {
                    waiting = null
                    run(it)
                }
            } else if (method == "popup.close" && isOpen) {
                waiting = null
                view.visibility = View.INVISIBLE
                onClose()
            }
            answer(id, """{"ok":true,"result":null}""")
            return
        }
        MiningKit.work.execute {
            val text = MiningKit.callRaw(method, params)
            main.post { answer(id, text) }
        }
    }

    /** The kit's answers are JSON, which is also how JavaScript writes an object. */
    private fun answer(id: Int, json: String) = run("window.oreAndroidAnswer($id, $json)")

    // ---------------------------------------------------------------- the page's files, and a page that broke

    private inner class Pages : WebViewClient() {
        override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse =
            if (request.url.host == HOST) file(request.url.path.orEmpty(), request.url.getQueryParameter("p")) ?: none()
            else none()

        // The page stays where it is: a link in a definition leads nowhere.
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean =
            request.url.host != HOST

        /** Without its page the popup would hold the game for good: it closes and the page is loaded again. */
        override fun onRenderProcessGone(view: WebView, detail: RenderProcessGoneDetail): Boolean {
            Log.w("Oretsuba", "mining: the popup's page broke")
            ready = false
            close()
            this@MiningPopup.view.removeAllViews()
            view.destroy()
            web = null
            if (!destroyed) load()
            return true
        }
    }

    /** A file for the page: one of its own from the package, or one of the kit's. Comes on a thread of the web view's. */
    private fun file(path: String, params: String?): WebResourceResponse? {
        if (path == "/android-host.js") return found(HOST_SCRIPT.toByteArray(), "text/javascript")
        if (path.startsWith("/kit/data/")) {
            // A dictionary's picture, the line's picture or voice, a word's sound.
            val data = MiningKit.data(path.removePrefix("/kit/data/"), params ?: "{}") ?: return null
            return found(data.first, data.second.substringBefore(';').trim())
        }
        // A file of the page's own, and nothing outside its folder.
        val name = path.removePrefix("/")
        if (".." in name) return null
        val type = TYPES[name.substringAfterLast('.', "")] ?: return null
        val bytes = try {
            activity.assets.open("mining/page/$name").use { it.readBytes() }
        } catch (e: IOException) {
            return null
        }
        return found(if (name == "popup.html") withHost(bytes) else bytes, type)
    }

    private fun found(bytes: ByteArray, type: String): WebResourceResponse {
        val text = type.startsWith("text/") || type == "application/json"
        return WebResourceResponse(type, if (text) "utf-8" else null, 200, "OK",
                                   mapOf("Cache-Control" to "no-store"), ByteArrayInputStream(bytes))
    }

    private fun none() = WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(),
                                             ByteArrayInputStream(ByteArray(0)))

    /**
     * The page finds its host as window.oreHost, which has to be there before
     * the page's own script runs: the script that sets it is named first in
     * the page's head.
     */
    private fun withHost(page: ByteArray): ByteArray {
        val text = String(page, Charsets.UTF_8)
        val head = text.indexOf("<head>")
        if (head < 0) return page
        val at = head + "<head>".length
        return (text.substring(0, at) + "<script src=\"android-host.js\"></script>" + text.substring(at))
            .toByteArray(Charsets.UTF_8)
    }

    companion object {
        /** A name that no server has: what the page asks for is answered here or not at all. */
        private const val HOST = "mining.oretsuba.invalid"
        private const val PAGE = "https://$HOST/popup.html"

        private val TYPES = mapOf("html" to "text/html", "js" to "text/javascript", "css" to "text/css",
                                  "json" to "application/json")

        /**
         * How the page reaches the kit here (the page's host.js): a call is
         * posted to the app with a number, and the app answers that number.
         */
        private const val HOST_SCRIPT = """(() => {
  const waiting = new Map();
  let next = 1;
  window.oreAndroidAnswer = (id, answer) => {
    const done = waiting.get(id);
    waiting.delete(id);
    if (done) done(answer);
  };
  window.oreHost = {
    call(method, params) {
      return new Promise((resolve) => {
        const id = next++;
        waiting.set(id, resolve);
        window.oreAndroid.post(id, method, params);
      });
    },
  };
})();
"""

        /**
         * The page is written for a browser of the last years. Android's web
         * view is one, kept new by the Play Store; a device that never
         * updated it may have one too old. Null when it will do, otherwise a
         * sentence for the player.
         */
        fun problem(): String? {
            val version = WebView.getCurrentWebViewPackage()?.versionName ?: return null
            val major = version.substringBefore('.').toIntOrNull() ?: return null
            return if (major >= OLDEST) null
            else "The dictionary needs a newer Android System WebView (this one is $major, it needs $OLDEST). Update it in the Play Store."
        }

        /** Chromium 88 knows everything the page and the popup inside it use. */
        private const val OLDEST = 88
    }
}

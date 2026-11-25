package io.github.friedrich_de.oretsuba

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Choreographer
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewConfiguration
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout
import android.window.OnBackInvokedDispatcher
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The game screen: the engine's 800x600 picture, scaled to fit with black
 * bars, and every kind of input turned into the mouse and keyboard the game
 * expects.
 *
 *   touch
 *     tap                 left click
 *     two-finger tap      right click (the game's menu / hides the text window)
 *     swipe down / up     mouse wheel (the backlog)
 *     press and hold      Ctrl while held (skip)
 *   back (button or gesture)   right click
 *   mouse or trackpad     move, left / right click and scroll as on a PC
 *   keyboard              Return, Space, Escape, arrows, Ctrl ...
 *   game controller       A = left click, B = right click, right trigger = skip,
 *                         d-pad up / down = wheel
 *
 * The engine runs on a thread of its own, once per display frame; everything
 * that touches it is handed to that thread.
 */
class GameActivity : Activity(), SurfaceHolder.Callback, EngineHost, Choreographer.FrameCallback {
    private val main = Handler(Looper.getMainLooper())
    private val engineThread = EngineThread()
    private lateinit var picture: PictureView
    private var session = 0L

    // Owned by the engine's thread.
    private var booted = false
    private var visible = false
    private var framePosted = false
    private var stopped = false

    // The game is played by touch, mouse, keys or a controller; a screen reader has nothing to press here.
    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val folder = intent.getStringExtra(GAME_FOLDER)
        if (folder == null) {
            finish()
            return
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        // The picture stands in the middle of the whole screen, with a camera hole in
        // one of its black bars. From Android 15 on the content reaches from edge to
        // edge by itself; before that the system would keep it clear of the hole, which
        // moves the picture off the middle.
        if (Build.VERSION.SDK_INT in 30..34) {
            @Suppress("DEPRECATION")
            window.setDecorFitsSystemWindows(false)
        }
        picture = PictureView(this)
        picture.holder.setFixedSize(GAME_WIDTH, GAME_HEIGHT)
        picture.holder.setFormat(PixelFormat.RGBX_8888)
        picture.holder.addCallback(this)
        val frame = FrameLayout(this)
        frame.setBackgroundColor(Color.BLACK)
        frame.addView(picture, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                                                        FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER))
        frame.isFocusable = true
        frame.isFocusableInTouchMode = true
        frame.setOnTouchListener { _, event -> onTouch(event) }
        frame.setOnHoverListener { _, event -> onHover(event) }
        frame.setOnGenericMotionListener { _, event -> onGenericMotion(event) }
        setContentView(frame)
        frame.requestFocus()
        hideSystemBars()
        if (Build.VERSION.SDK_INT >= 33) {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT) {
                cancel(null)
            }
        }

        val paths = GamePaths(this)
        val saves = paths.saves.path
        val keepsPlace = paths.keepsPlace
        session = Native.newSession(this)
        engineThread.startAndWait()
        engineThread.handler.post {
            val failure = Native.boot(session, folder.toByteArray(), saves.toByteArray(),
                                      if (keepsPlace) GamePaths.AUTOSAVE_LINES else 0, keepsPlace)
            if (failure == null) booted = true else leave(String(failure, Charsets.UTF_8))
        }
    }

    override fun onStart() {
        super.onStart()
        if (session == 0L) return
        engineThread.handler.post {
            visible = true
            if (booted) {
                Native.setPaused(session, false)
                postFrame()
            }
        }
    }

    /**
     * Leaving the screen stops the game. That is also when the engine saves
     * the reader's place and sends the line on the screen to the mining
     * server, so wait a moment for it: the system may stop the app right after.
     */
    override fun onStop() {
        super.onStop()
        if (session == 0L) return
        val done = CountDownLatch(1)
        val posted = engineThread.handler.post {
            visible = false
            if (booted) Native.setPaused(session, true)
            done.countDown()
        }
        if (posted) done.await(1500, TimeUnit.MILLISECONDS)
    }

    override fun onDestroy() {
        super.onDestroy()
        if (session == 0L) return
        main.removeCallbacksAndMessages(null)
        picture.holder.removeCallback(this)
        Native.setSurface(session, null)
        val closing = session
        engineThread.handler.post {
            stopped = true
            Native.destroy(closing)
            Looper.myLooper()?.quit()
        }
        // The engine saves the reader's place and lets lines on their way to the server finish.
        engineThread.join(3000)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus && session != 0L) hideSystemBars()
    }

    private fun hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.let {
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                it.hide(WindowInsets.Type.systemBars())
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility =
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
    }

    /** Back to the launcher, with the reason when the game did not end by itself. Any thread. */
    private fun leave(error: String?) {
        main.post {
            if (isFinishing) return@post
            setResult(RESULT_OK, Intent().putExtra(ERROR, error))
            finish()
        }
    }

    // ---------------------------------------------------------------- frames and the picture

    private fun postFrame() {
        if (framePosted || stopped) return
        framePosted = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    /** Once per display frame, on the engine's thread. */
    override fun doFrame(frameTimeNanos: Long) {
        framePosted = false
        if (!booted || !visible || stopped) return
        val result = Native.frame(session)
        Native.present(session)
        if (result != 0) {
            stopped = true
            leave(if (result < 0) String(Native.lastError(session), Charsets.UTF_8) else null)
            return
        }
        postFrame()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        Native.setSurface(session, holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        engine { Native.present(session) }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Native.setSurface(session, null)
    }

    /** Runs [block] on the engine's thread once the game is running. */
    private inline fun engine(crossinline block: () -> Unit) {
        engineThread.handler.post { if (booted && !stopped) block() }
    }

    // ---------------------------------------------------------------- message boxes and text entry asked for by the game

    override fun onDialog(text: ByteArray, kind: Int) {
        val message = String(text, Charsets.UTF_8)
        main.post {
            if (isFinishing || isDestroyed) return@post
            val answer = { result: Int -> engine { Native.answerDialog(session, result) } }
            val dialog = AlertDialog.Builder(this).setMessage(message).setCancelable(false)
            if (kind == 1) {
                dialog.setNegativeButton(R.string.no) { _, _ -> answer(0) }
                dialog.setPositiveButton(R.string.yes) { _, _ -> answer(1) }
            } else {
                dialog.setPositiveButton(R.string.ok) { _, _ -> answer(1) }
            }
            dialog.show()
        }
    }

    override fun onTextInput(initial: ByteArray) {
        val value = String(initial, Charsets.UTF_8)
        main.post {
            if (isFinishing || isDestroyed) return@post
            val field = EditText(this)
            field.inputType = InputType.TYPE_CLASS_TEXT
            field.setText(value)
            field.setSelection(field.text.length)
            AlertDialog.Builder(this).setTitle(R.string.comment).setView(field).setCancelable(false)
                .setNegativeButton(R.string.cancel) { _, _ -> engine { Native.answerTextInput(session, false, null) } }
                .setPositiveButton(R.string.ok) { _, _ ->
                    val typed = field.text.toString().toByteArray()
                    engine { Native.answerTextInput(session, true, typed) }
                }
                .show()
        }
    }

    // ---------------------------------------------------------------- touch

    private var downX = 0f
    private var downY = 0f
    private var lastY = 0f
    private var moved = false
    private var fingers = 0
    private var wheel = 0f
    private var holdingCtrl = false
    private val hold = Runnable { if (!moved && fingers == 1) setCtrl(true) }

    /** A point of the screen in game pixels, clamped to the picture. */
    private fun gamePoint(x: Float, y: Float): Pair<Int, Int> {
        if (picture.width <= 0 || picture.height <= 0) return 0 to 0
        val gx = ((x - picture.left) / picture.width * GAME_WIDTH).toInt().coerceIn(0, GAME_WIDTH - 1)
        val gy = ((y - picture.top) / picture.height * GAME_HEIGHT).toInt().coerceIn(0, GAME_HEIGHT - 1)
        return gx to gy
    }

    private fun onTouch(event: MotionEvent): Boolean {
        if (event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE) return onMouse(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.x
                downY = event.y
                lastY = event.y
                moved = false
                fingers = 1
                wheel = 0f
                main.postDelayed(hold, HOLD_MS)
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                fingers = maxOf(fingers, event.pointerCount)
                main.removeCallbacks(hold)
            }
            MotionEvent.ACTION_MOVE -> {
                val slop = ViewConfiguration.get(this).scaledTouchSlop
                if (!moved && hypot(event.x - downX, event.y - downY) > slop) {
                    moved = true
                    main.removeCallbacks(hold)
                }
                if (moved && fingers == 1 && !holdingCtrl) swipe(event.y)
            }
            MotionEvent.ACTION_UP -> {
                main.removeCallbacks(hold)
                when {
                    holdingCtrl -> setCtrl(false)
                    !moved && fingers >= 2 -> cancel(gamePoint(downX, downY))
                    !moved -> click(LEFT, gamePoint(downX, downY))
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                main.removeCallbacks(hold)
                setCtrl(false)
            }
        }
        return true
    }

    /**
     * Vertical swipes become wheel notches, one for every 40 dp. Pulling down
     * (the content follows the finger) = wheel up = the backlog.
     */
    private fun swipe(y: Float) {
        val notch = 40 * resources.displayMetrics.density
        wheel += y - lastY
        lastY = y
        while (abs(wheel) >= notch) {
            val delta = if (wheel > 0) 120 else -120
            engine { Native.mouseWheel(session, delta) }
            wheel -= if (wheel > 0) notch else -notch
        }
    }

    /** A tap is a press now and a release three frames later, so the game sees both. */
    private fun click(button: Int, at: Pair<Int, Int>?) {
        engine {
            if (at != null) Native.mouseMove(session, at.first, at.second)
            Native.mouseButton(session, button, true)
        }
        engineThread.handler.postDelayed({ if (booted && !stopped) Native.mouseButton(session, button, false) }, 50)
    }

    /**
     * The right button of a player without a mouse: the tap with two fingers, Back,
     * a controller's B. While a film plays it is Escape instead, the one key that
     * ends a film early on the PC, which a phone does not have.
     */
    private fun cancel(at: Pair<Int, Int>?) {
        engine {
            if (Native.moviePlaying(session)) {
                Native.key(session, VK_ESCAPE, true)
                Native.key(session, VK_ESCAPE, false)
            } else {
                if (at != null) Native.mouseMove(session, at.first, at.second)
                Native.mouseButton(session, RIGHT, true)
                engineThread.handler.postDelayed({ if (booted && !stopped) Native.mouseButton(session, RIGHT, false) }, 50)
            }
        }
    }

    private fun setCtrl(down: Boolean) {
        if (down == holdingCtrl) return
        holdingCtrl = down
        engine { Native.key(session, VK_CONTROL, down) }
    }

    // ---------------------------------------------------------------- mouse and trackpad: real presses

    private var mouseButton = -1

    private fun onMouse(event: MotionEvent): Boolean {
        val (x, y) = gamePoint(event.x, event.y)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val button = when {
                    event.buttonState and MotionEvent.BUTTON_SECONDARY != 0 -> RIGHT
                    event.buttonState and MotionEvent.BUTTON_TERTIARY != 0 -> MIDDLE
                    else -> LEFT
                }
                mouseButton = button
                engine {
                    Native.mouseMove(session, x, y)
                    Native.mouseButton(session, button, true)
                }
            }
            MotionEvent.ACTION_MOVE -> engine { Native.mouseMove(session, x, y) }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val button = mouseButton
                mouseButton = -1
                if (button >= 0) {
                    engine {
                        Native.mouseMove(session, x, y)
                        Native.mouseButton(session, button, false)
                    }
                }
            }
        }
        return true
    }

    private fun onHover(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_HOVER_MOVE) {
            val (x, y) = gamePoint(event.x, event.y)
            engine { Native.mouseMove(session, x, y) }
        }
        return true
    }

    private var hatY = 0f

    /** The mouse wheel, and a game controller's d-pad and right trigger where they come as axes. */
    private fun onGenericMotion(event: MotionEvent): Boolean {
        if (event.isFromSource(InputDevice.SOURCE_CLASS_POINTER)) {
            if (event.actionMasked != MotionEvent.ACTION_SCROLL) return false
            val delta = (event.getAxisValue(MotionEvent.AXIS_VSCROLL) * 120).roundToInt()
            if (delta != 0) engine { Native.mouseWheel(session, delta) }
            return true
        }
        if (event.isFromSource(InputDevice.SOURCE_JOYSTICK) && event.actionMasked == MotionEvent.ACTION_MOVE) {
            val y = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
            if (y != hatY) {
                hatY = y
                if (y < -0.5f) engine { Native.mouseWheel(session, 120) }
                if (y > 0.5f) engine { Native.mouseWheel(session, -120) }
            }
            val trigger = maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS))
            setCtrl(trigger > 0.5f)
            return true
        }
        return false
    }

    // ---------------------------------------------------------------- keys, game controllers, back

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (session == 0L) return super.dispatchKeyEvent(event)
        val down = when (event.action) {
            KeyEvent.ACTION_DOWN -> true
            KeyEvent.ACTION_UP -> false
            else -> return super.dispatchKeyEvent(event)
        }
        val pad = event.isFromSource(InputDevice.SOURCE_GAMEPAD) || event.isFromSource(InputDevice.SOURCE_JOYSTICK)
        when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> button(LEFT, down, event)
            KeyEvent.KEYCODE_BUTTON_B -> if (down && event.repeatCount == 0) cancel(null)
            KeyEvent.KEYCODE_BUTTON_R2 -> setCtrl(down)
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN -> {
                if (!pad) return key(event, down)
                val delta = if (event.keyCode == KeyEvent.KEYCODE_DPAD_UP) 120 else -120
                if (down) engine { Native.mouseWheel(session, delta) }
            }
            else -> return key(event, down)
        }
        return true
    }

    private fun button(button: Int, down: Boolean, event: KeyEvent) {
        if (event.repeatCount == 0) engine { Native.mouseButton(session, button, down) }
    }

    private fun key(event: KeyEvent, down: Boolean): Boolean {
        val vk = virtualKey(event.keyCode) ?: return super.dispatchKeyEvent(event)
        if (event.repeatCount == 0) engine { Native.key(session, vk, down) }
        return true
    }

    /** Below Android 13 the back button comes here; from 13 on through the callback set in onCreate. */
    @Deprecated("Deprecated in Java")
    @SuppressLint("GestureBackNavigation")
    override fun onBackPressed() {
        if (session != 0L) cancel(null) else finish()
    }

    // ---------------------------------------------------------------- the pieces

    /** The game's view: as large as fits at the picture's 4:3. */
    private class PictureView(context: Context) : SurfaceView(context) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val width = MeasureSpec.getSize(widthMeasureSpec)
            val height = MeasureSpec.getSize(heightMeasureSpec)
            val scale = min(width / GAME_WIDTH.toFloat(), height / GAME_HEIGHT.toFloat())
            setMeasuredDimension((GAME_WIDTH * scale).roundToInt(), (GAME_HEIGHT * scale).roundToInt())
        }
    }

    /** The engine's thread: a message loop with a stack as deep as a main thread's. */
    private class EngineThread : Thread(null, null, "engine", 8L shl 20) {
        lateinit var handler: Handler
            private set
        private val ready = CountDownLatch(1)

        override fun run() {
            Looper.prepare()
            handler = Handler(Looper.myLooper()!!)
            ready.countDown()
            Looper.loop()
        }

        fun startAndWait() {
            start()
            ready.await()
        }
    }

    companion object {
        const val GAME_FOLDER = "gameFolder"
        const val ERROR = "error"
        private const val GAME_WIDTH = 800
        private const val GAME_HEIGHT = 600
        private const val HOLD_MS = 600L
        private const val LEFT = 0
        private const val RIGHT = 1
        private const val MIDDLE = 2
        private const val VK_CONTROL = 0x11
        private const val VK_ESCAPE = 0x1B

        /** Windows virtual-key codes of the keys a player may press. */
        private fun virtualKey(code: Int): Int? = when (code) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_DPAD_CENTER -> 0x0D
            KeyEvent.KEYCODE_SPACE -> 0x20
            KeyEvent.KEYCODE_ESCAPE -> 0x1B
            KeyEvent.KEYCODE_TAB -> 0x09
            KeyEvent.KEYCODE_DEL -> 0x08
            KeyEvent.KEYCODE_DPAD_LEFT -> 0x25
            KeyEvent.KEYCODE_DPAD_UP -> 0x26
            KeyEvent.KEYCODE_DPAD_RIGHT -> 0x27
            KeyEvent.KEYCODE_DPAD_DOWN -> 0x28
            KeyEvent.KEYCODE_PAGE_UP -> 0x21
            KeyEvent.KEYCODE_PAGE_DOWN -> 0x22
            KeyEvent.KEYCODE_MOVE_HOME -> 0x24
            KeyEvent.KEYCODE_MOVE_END -> 0x23
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 0x11
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x10
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> 0x12
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> 0x41 + (code - KeyEvent.KEYCODE_A)
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> 0x30 + (code - KeyEvent.KEYCODE_0)
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> 0x70 + (code - KeyEvent.KEYCODE_F1)
            else -> null
        }
    }
}

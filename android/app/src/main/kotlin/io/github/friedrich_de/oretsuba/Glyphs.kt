package io.github.friedrich_de.oretsuba

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale

/**
 * The game's letters, drawn by the system with the device's Japanese fonts
 * (the gothic face, or the mincho one when the game asks for ＭＳ 明朝), as the
 * Apple port draws them with the system's Hiragino faces.
 */
internal object Glyphs {
    private val paints = HashMap<Int, Paint>()
    private val bounds = Rect()
    private val chars = CharArray(2)

    private fun paint(serif: Boolean, bold: Boolean, size: Int): Paint {
        val key = size shl 2 or (if (serif) 2 else 0) or (if (bold) 1 else 0)
        return paints.getOrPut(key) {
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                typeface = Typeface.create(if (serif) Typeface.SERIF else Typeface.SANS_SERIF,
                                           if (bold) Typeface.BOLD else Typeface.NORMAL)
                textSize = size.toFloat()
                // Among the fonts that share Chinese characters, take the Japanese shapes.
                textLocale = Locale.JAPAN
            }
        }
    }

    /**
     * One letter at an em size of [size] pixels: five little-endian 32-bit
     * numbers (width, height, the bitmap's left edge from the pen, its top
     * edge above the baseline, the pen's advance), then width * height bytes
     * of coverage. Null when no font has the letter.
     */
    @Synchronized
    fun render(codepoint: Int, serif: Boolean, bold: Boolean, size: Int): ByteArray? {
        if (!Character.isValidCodePoint(codepoint) || size <= 0) return null
        val paint = paint(serif, bold, size)
        val text = String(chars, 0, Character.toChars(codepoint, chars, 0))
        if (!paint.hasGlyph(text)) return null
        paint.getTextBounds(text, 0, text.length, bounds)
        // One pixel of room on every side for the antialiased edge.
        val width = if (bounds.isEmpty) 0 else bounds.width() + 2
        val height = if (bounds.isEmpty) 0 else bounds.height() + 2
        val left = bounds.left - 1
        val top = bounds.top - 1
        val out = ByteBuffer.allocate(20 + width * height).order(ByteOrder.LITTLE_ENDIAN)
        out.putInt(width).putInt(height).putInt(left).putInt(-top).putInt(Math.round(paint.measureText(text)))
        if (width > 0 && height > 0) {
            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ALPHA_8)
            Canvas(bitmap).drawText(text, -left.toFloat(), -top.toFloat(), paint)
            val stride = bitmap.rowBytes
            val pixels = ByteBuffer.allocate(stride * height)
            bitmap.copyPixelsToBuffer(pixels)
            bitmap.recycle()
            val rows = pixels.array()
            for (y in 0 until height) out.put(rows, y * stride, width)
        }
        return out.array()
    }
}

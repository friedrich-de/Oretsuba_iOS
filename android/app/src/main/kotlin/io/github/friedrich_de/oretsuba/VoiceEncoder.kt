package io.github.friedrich_de.oretsuba

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.os.SystemClock
import android.util.Log
import java.io.File

/**
 * A line's voice for a card (docs/MINING.md). The game's voices are Ogg
 * Vorbis, which Anki on an iPhone does not play; native code decodes one and
 * this writes it as AAC in an MPEG-4 file, which every Anki plays, with the
 * system's own encoder.
 */
internal object VoiceEncoder {
    private const val BIT_RATE = 128_000
    private const val WAIT_US = 10_000L
    /** A voice is some seconds long. An encoder that takes longer than this has hung. */
    private const val LIMIT_MS = 30_000L
    /** Three frames of silence, in bytes: the encoder's delay of two frames, and the frame the voice ends in. */
    private const val SILENCE_BYTES = 3 * 1024 * 4

    /**
     * [voice] holds 16-bit little-endian samples, two channels in turn. Null when the
     * device cannot encode them; the card then carries the game's own file.
     */
    fun encode(voice: ByteArray, rate: Int): ByteArray? {
        val folder = MiningKit.context?.cacheDir ?: return null
        if (voice.size < 4 || rate <= 0) return null
        // The encoder gives out whole frames of 1024 moments only, and each 2048 moments late:
        // without silence behind the voice, its last 50 to 70 ms would stay inside the encoder.
        val samples = voice.copyOf((voice.size and 3.inv()) + SILENCE_BYTES)
        val file = File.createTempFile("ore-voice", ".m4a", folder)
        var codec: MediaCodec? = null
        var muxer: MediaMuxer? = null
        var muxing = false
        try {
            val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, rate, 2)
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            format.setInteger(MediaFormat.KEY_BIT_RATE, BIT_RATE)
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16 * 1024)
            codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            codec.start()
            muxer = MediaMuxer(file.path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            val info = MediaCodec.BufferInfo()
            val bytesPerSecond = rate.toLong() * 4
            val end = SystemClock.elapsedRealtime() + LIMIT_MS
            var track = -1
            var offset = 0
            var fed = false
            var done = false
            while (!done) {
                if (SystemClock.elapsedRealtime() > end) return null
                if (!fed) {
                    val index = codec.dequeueInputBuffer(WAIT_US)
                    if (index >= 0) {
                        val buffer = codec.getInputBuffer(index) ?: return null
                        buffer.clear()
                        // Whole frames only: four bytes are one moment of both channels.
                        val count = minOf(buffer.remaining(), samples.size - offset) and 3.inv()
                        val time = offset * 1_000_000L / bytesPerSecond
                        if (count <= 0) {
                            codec.queueInputBuffer(index, 0, 0, time, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            fed = true
                        } else {
                            buffer.put(samples, offset, count)
                            codec.queueInputBuffer(index, 0, count, time, 0)
                            offset += count
                        }
                    }
                }
                val index = codec.dequeueOutputBuffer(info, WAIT_US)
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    track = muxer.addTrack(codec.outputFormat)
                    muxer.start()
                    muxing = true
                } else if (index >= 0) {
                    val buffer = codec.getOutputBuffer(index) ?: return null
                    // The encoder's own settings are in the track's format already.
                    val settings = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
                    if (!settings && info.size > 0 && muxing) {
                        buffer.position(info.offset)
                        buffer.limit(info.offset + info.size)
                        muxer.writeSampleData(track, buffer, info)
                    }
                    done = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    codec.releaseOutputBuffer(index, false)
                }
            }
            if (!muxing) return null
            muxer.stop()
            muxing = false
            muxer.release()
            muxer = null
            return file.readBytes().takeIf { it.isNotEmpty() }
        } catch (e: Exception) {
            Log.w("Oretsuba", "mining: the voice could not be encoded: $e")
            return null
        } finally {
            try {
                codec?.stop()
            } catch (e: Exception) {
                // It never started, or it is in a state that has nothing to stop.
            }
            codec?.release()
            try {
                if (muxing) muxer?.stop()
            } catch (e: Exception) {
                // A file without a sample cannot be finished; it is deleted below.
            }
            muxer?.release()
            file.delete()
        }
    }
}

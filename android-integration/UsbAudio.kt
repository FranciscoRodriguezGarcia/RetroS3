package com.retro.retrocontroller

import android.hardware.usb.*
import android.media.*
import android.os.SystemClock
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit

/** Owns only BULK IN. All BULK OUT writes remain on the controller IO coroutine. */
class UsbAudio(private val connection: UsbDeviceConnection, private val endpoint: UsbEndpoint) {
    @Volatile private var running = true
    private class PcmBlock {
        val bytes = ByteArray(768)
        var count = 0
        var receivedAt = 0L
    }
    // Three queued blocks + one being decoded + one being played.
    private val queue = ArrayBlockingQueue<PcmBlock>(3)
    private val pool = ArrayBlockingQueue<PcmBlock>(5).also { q -> repeat(5) { q.add(PcmBlock()) } }
    private val dropped = java.util.concurrent.atomic.AtomicLong()
    @Volatile private var underruns = 0L
    @Volatile private var maxQueueMs = 0L
    @Volatile private var sinkMs = 0L
    @Volatile private var generation = 0
    @Volatile var summary = "AUDIO: WAIT"
        private set
    private var reader: Thread? = null
    private var player: Thread? = null
    private fun clearQueue() {
        while (true) pool.offer(queue.poll() ?: break)
    }
    fun start() {
        reader = Thread({
            val chunk = ByteArray(2048)
            var packets = 0L
            var lastSequence = -1L
            var lastReport = 0L
            var mode = "AUDIO"
            val decoder = UsbPacketDecoder { type, bytes, offset, count ->
                val sequence = u32(bytes, 4)
                if (lastSequence >= 0 && sequence < lastSequence) {
                    clearQueue()
                    generation++
                }
                lastSequence = sequence
                if (type == 6 && count == 52) {
                    mode = if (u32(bytes, offset) == 1L) "TONE 440Hz" else "AUDIO"
                    android.util.Log.i("RetroAudio", "ESP mode=$mode blocks=${u32(bytes, offset+4)} " +
                        "drop=${u32(bytes, offset+8)} stale=${u32(bytes, offset+12)} txFail=${u32(bytes, offset+16)} " +
                        "badRx=${u32(bytes, offset+20)} queueMax=${u32(bytes, offset+24)} ageMaxUs=${u32(bytes, offset+28)} " +
                        "internalFree=${u32(bytes, offset+32)} psramFree=${u32(bytes, offset+36)} " +
                        "internalMin=${u32(bytes, offset+40)} txStackFree=${u32(bytes, offset+44)} starvation=${u32(bytes, offset+48)}")
                }
                if (type == 1 && count > 0 && count <= 768 && count % 2 == 0) {
                    packets++
                    var block = pool.poll()
                    if (block == null) {
                        block = queue.poll()
                        dropped.incrementAndGet()
                    }
                    if (block != null) {
                        System.arraycopy(bytes, offset, block.bytes, 0, count)
                        block.count = count
                        block.receivedAt = SystemClock.elapsedRealtime()
                        if (!queue.offer(block)) {
                            queue.poll()?.let { pool.offer(it) }
                            dropped.incrementAndGet()
                            if (!queue.offer(block)) pool.offer(block)
                        }
                    }
                }
            }
            try {
                while (running) {
                    val n = connection.bulkTransfer(endpoint, chunk, chunk.size, 100)
                    if (n > 0) decoder.accept(chunk, n) else if (n < 0) Thread.sleep(10)
                    val now = SystemClock.elapsedRealtime()
                    if (now - lastReport >= 1000) {
                        summary = "$mode RX:$packets DROP:${dropped.get()}"
                        android.util.Log.i("RetroAudio", "Android packets=$packets drop=${dropped.get()} " +
                            "badRx=${decoder.rejected} underruns=$underruns queueMaxMs=$maxQueueMs sinkMs=$sinkMs")
                        lastReport = now
                    }
                }
            } catch (e: Exception) {
                android.util.Log.e("RetroAudio", "USB receive failed", e)
                running = false
            }
        }, "retro-usb-rx").also { it.start() }
        player = Thread({
            var track: AudioTrack? = null
            var last = SystemClock.elapsedRealtime()
            var seenGeneration = generation
            var writtenFrames = 0L
            var previousUnderruns = 0
            try {
                while (running) {
                    if (generation != seenGeneration) {
                        track?.pause(); track?.flush(); track?.release(); track = null
                        seenGeneration = generation
                    }
                    val pcm = queue.poll(100, TimeUnit.MILLISECONDS)
                    if (pcm == null) {
                        if (SystemClock.elapsedRealtime() - last > 500) {
                            track?.pause(); track?.flush(); track?.release(); track = null
                            summary = "AUDIO: WAIT"
                        }
                        continue
                    }
                    try {
                        last = SystemClock.elapsedRealtime()
                        val age = last - pcm.receivedAt
                        maxQueueMs = maxOf(maxQueueMs, age)
                        if (age > 60) { dropped.incrementAndGet(); continue }
                        if (track == null) {
                            val minimum = AudioTrack.getMinBufferSize(22050, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
                            require(minimum > 0)
                            track = AudioTrack.Builder()
                                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                                .setAudioFormat(AudioFormat.Builder().setSampleRate(22050).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(maxOf(minimum, 3528)).build()
                            check(track.state == AudioTrack.STATE_INITIALIZED)
                            writtenFrames = 0
                            previousUnderruns = 0
                            track.play()
                        }
                        val result = track.write(pcm.bytes, 0, pcm.count, AudioTrack.WRITE_NON_BLOCKING)
                        if (result < pcm.count) dropped.incrementAndGet()
                        if (result < 0) { track.release(); track = null }
                        else {
                            writtenFrames += result / 2
                            val head = track.playbackHeadPosition.toLong() and 0xffffffffL
                            sinkMs = maxOf(0, writtenFrames - head) * 1000 / 22050
                            val current = track.underrunCount
                            underruns += maxOf(0, current - previousUnderruns)
                            previousUnderruns = current
                        }
                    } finally { pool.offer(pcm) }
                }
            } catch (e: Exception) {
                android.util.Log.e("RetroAudio", "Audio playback failed", e)
                running = false
            } finally { track?.pause(); track?.flush(); track?.release() }
        }, "retro-audio").also { it.start() }
    }
    fun isRunning(): Boolean = running
    fun stop() {
        running = false
        reader?.join(300); player?.join(300); clearQueue()
    }
    companion object {
        private fun u32(bytes: ByteArray, offset: Int): Long {
            var value = 0L
            for (i in 0..3) value = value or ((bytes[offset+i].toLong() and 255) shl (8*i))
            return value
        }
        fun crc(bytes: ByteArray, count: Int): Int {
            var crc = 65535
            for (i in 0 until count) {
                crc = crc xor ((bytes[i].toInt() and 255) shl 8)
                repeat(8) { crc = ((crc shl 1) xor if (crc and 32768 != 0) 0x1021 else 0) and 65535 }
            }
            return crc
        }
        fun heartbeat(): ByteArray = packet(3, 1)
        fun packet(type: Int, value: Int): ByteArray {
            val raw = byteArrayOf(1,type.toByte(),1,0,0,0,0,0,value.toByte(),0,0)
            val crc = crc(raw,9); raw[9]=crc.toByte(); raw[10]=(crc shr 8).toByte()
            val wire = mutableListOf(192.toByte())
            raw.forEach {
                when (it.toInt() and 255) {
                    192 -> { wire.add(219.toByte()); wire.add(220.toByte()) }
                    219 -> { wire.add(219.toByte()); wire.add(221.toByte()) }
                    else -> wire.add(it)
                }
            }
            wire.add(192.toByte()); return wire.toByteArray()
        }
    }
}

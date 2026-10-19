package com.retro.retrocontroller

/** Incremental SLIP decoder. Bounded even for malformed or unterminated input. */
class UsbPacketDecoder(private val packet: (Int, ByteArray, Int, Int) -> Unit) {
    private val frame = ByteArray(778)
    private var size = 0
    private var escaped = false
    private var invalid = false
    var rejected: Long = 0
        private set
    fun accept(bytes: ByteArray, count: Int = bytes.size) {
        for (i in 0 until count) {
            var b = bytes[i].toInt() and 255
            if (b == 192) {
                var valid = false
                if (!invalid && !escaped && size >= 10 && frame[0].toInt() == 1) {
                    val len = (frame[2].toInt() and 255) or ((frame[3].toInt() and 255) shl 8)
                    if (len + 10 == size && UsbAudio.crc(frame, size - 2) ==
                        ((frame[size-2].toInt() and 255) or ((frame[size-1].toInt() and 255) shl 8))) {
                        // Borrowed storage: the callback must consume/copy synchronously.
                        packet(frame[1].toInt() and 255, frame, 8, len)
                        valid = true
                    }
                }
                if (!valid && (size > 0 || invalid || escaped)) rejected++
                size = 0; escaped = false; invalid = false
            } else if (b == 219) {
                if (escaped) invalid = true
                escaped = true
            } else {
                if (escaped) {
                    if (b != 220 && b != 221) invalid = true
                    b = if (b == 220) 192 else 219
                    escaped = false
                }
                if (size < frame.size) frame[size++] = b.toByte() else invalid = true
            }
        }
    }
}

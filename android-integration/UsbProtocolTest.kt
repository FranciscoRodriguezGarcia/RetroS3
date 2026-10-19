package com.retro.retrocontroller

import org.junit.Assert.*
import org.junit.Test

class UsbProtocolTest {
    @Test fun standardCrcVector() {
        assertEquals(0x29b1, UsbAudio.crc("123456789".toByteArray(), 9))
    }
    @Test fun everyButtonMaskSurvivesByteFragmentation() {
        val received = mutableListOf<Int>()
        val decoder = UsbPacketDecoder { type, payload, offset, length ->
            assertEquals(4, type)
            assertEquals(1, length)
            received.add(payload[offset].toInt() and 255)
        }
        for (mask in 0..255) {
            UsbAudio.packet(4, mask).forEach { decoder.accept(byteArrayOf(it)) }
        }
        assertEquals((0..255).toList(), received)
    }
    @Test fun corruptTruncatedOversizedAndBootTextResynchronize() {
        val received = mutableListOf<Int>()
        val decoder = UsbPacketDecoder { type, _, _, _ -> received.add(type) }
        decoder.accept("ESP-ROM boot\n".toByteArray())
        val corrupt = UsbAudio.packet(5, 1)
        corrupt[9] = (corrupt[9].toInt() xor 1).toByte()
        decoder.accept(corrupt)
        decoder.accept(UsbAudio.packet(4, 192).copyOfRange(0, 6))
        decoder.accept(byteArrayOf(192.toByte()))
        decoder.accept(ByteArray(5000) { 7 })
        decoder.accept(byteArrayOf(192.toByte()))
        decoder.accept(UsbAudio.heartbeat())
        decoder.accept(UsbAudio.packet(5, 1))
        assertEquals(listOf(3,5), received)
        assertTrue(decoder.rejected >= 3)
    }
    @Test fun maximumEscapedPcmUsesBorrowedStorageAndSurvivesFragmentation() {
        val payload = ByteArray(768) { if (it % 2 == 0) 192.toByte() else 219.toByte() }
        val raw = ByteArray(778)
        raw[0] = 1; raw[1] = 1; raw[2] = 0; raw[3] = 3
        System.arraycopy(payload, 0, raw, 8, payload.size)
        val crc = UsbAudio.crc(raw, 776)
        raw[776] = crc.toByte(); raw[777] = (crc shr 8).toByte()
        val wire = mutableListOf(192.toByte())
        raw.forEach { b ->
            when (b.toInt() and 255) {
                192 -> { wire.add(219.toByte()); wire.add(220.toByte()) }
                219 -> { wire.add(219.toByte()); wire.add(221.toByte()) }
                else -> wire.add(b)
            }
        }
        wire.add(192.toByte())
        var calls = 0
        var storage: ByteArray? = null
        val decoder = UsbPacketDecoder { type, bytes, offset, length ->
            assertEquals(1, type)
            assertArrayEquals(payload, bytes.copyOfRange(offset, offset + length))
            if (storage != null) assertSame(storage, bytes)
            storage = bytes
            calls++
        }
        repeat(2) { wire.forEach { decoder.accept(byteArrayOf(it)) } }
        assertEquals(2, calls)
        assertEquals(0L, decoder.rejected)
    }
}

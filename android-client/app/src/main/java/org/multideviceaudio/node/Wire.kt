package org.multideviceaudio.node

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets

internal data class WirePacket(
    val type: Int, val sequence: Long = 0, val deviceId: Long = 0,
    val t1: Long = 0, val t2: Long = 0, val t3: Long = 0,
    val platform: Int = 2, val name: String = ""
)

internal object Wire {
    const val SIZE = 96
    const val DISCOVERY = 45670
    const val SYNC = 45671
    fun encode(p: WirePacket): ByteArray {
        val name = p.name.toByteArray(StandardCharsets.UTF_8)
        require(name.size <= 48 && p.type in 1..5)
        val b = ByteBuffer.allocate(SIZE).order(ByteOrder.BIG_ENDIAN)
        b.putInt(0x4d444131).putShort(1).putShort(p.type.toShort()).putInt(p.sequence.toInt())
        b.putLong(p.deviceId).putLong(p.t1).putLong(p.t2).putLong(p.t3)
        b.put(p.platform.toByte()).put(name.size.toByte()).putShort(0)
        b.put(name)
        return b.array()
    }
    fun decode(data: ByteArray, length: Int): WirePacket? {
        if (length != SIZE) return null
        val b = ByteBuffer.wrap(data, 0, length).order(ByteOrder.BIG_ENDIAN)
        if (b.int != 0x4d444131 || b.short.toInt() != 1) return null
        val type = b.short.toInt() and 0xffff
        if (type !in 1..5) return null
        val sequence = b.int.toLong() and 0xffffffffL
        val id = b.long; val t1 = b.long; val t2 = b.long; val t3 = b.long
        val platform = b.get().toInt() and 0xff
        val nameLength = b.get().toInt() and 0xff
        if (platform !in 1..5 || nameLength > 48 || b.short.toInt() != 0) return null
        val nameBytes = ByteArray(nameLength); b.get(nameBytes)
        while (b.hasRemaining()) if (b.get().toInt() != 0) return null
        if (nameBytes.any { it.toInt() == 0 }) return null
        val name = try {
            StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(nameBytes)).toString()
        } catch (_: Exception) { return null }
        return WirePacket(type, sequence, id, t1, t2, t3, platform, name)
    }
}

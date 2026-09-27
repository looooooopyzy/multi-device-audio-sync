package org.multideviceaudio.node

import android.content.Context
import java.net.Inet4Address
import java.net.NetworkInterface
import java.util.UUID

internal object DeviceIdentity {
    @Synchronized fun id(context: Context): Long {
        val prefs = context.getSharedPreferences("node", Context.MODE_PRIVATE)
        val existing = prefs.getLong("id", 0)
        if (existing != 0L) return existing
        val generated = UUID.randomUUID().mostSignificantBits.let { if (it == 0L) 1L else it }
        prefs.edit().putLong("id", generated).apply()
        return generated
    }
    fun localIPv4(): String = NetworkInterface.getNetworkInterfaces().toList().asSequence()
        .flatMap { it.inetAddresses.toList().asSequence() }
        .firstOrNull { it is Inet4Address && !it.isLoopbackAddress }?.hostAddress ?: "-"
}

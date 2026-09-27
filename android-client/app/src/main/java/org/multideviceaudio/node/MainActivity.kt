package org.multideviceaudio.node

import android.app.Activity
import android.content.*
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.widget.TextView
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var text: TextView
    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent == null) return
            val ip = DeviceIdentity.localIPv4()
            text.text = String.format(Locale.US,
                "Device Name: %s\nDevice ID: %s\nLocal IP: %s\nMaster IP: %s\nConnection Status: %s\nClock Offset: %+.3f ms\nRTT: %.3f ms\nDrift: %+.2f ppm\nSamples: %d",
                Build.MODEL, java.lang.Long.toUnsignedString(intent.getLongExtra("id", 0)), ip,
                intent.getStringExtra("master") ?: "-", intent.getStringExtra("status") ?: "-",
                intent.getDoubleExtra("offset", 0.0), intent.getDoubleExtra("rtt", 0.0),
                intent.getDoubleExtra("drift", 0.0), intent.getLongExtra("samples", 0))
        }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val id = java.lang.Long.toUnsignedString(DeviceIdentity.id(this))
        text = TextView(this).apply {
            textSize = 18f; setPadding(32, 32, 32, 32); gravity = Gravity.TOP
            text = "Device Name: ${Build.MODEL}\nDevice ID: $id\nLocal IP: ${DeviceIdentity.localIPv4()}\nMaster IP: -\nConnection Status: SEARCHING\nClock Offset: -\nRTT: -\nDrift: -\nSamples: 0"
        }
        setContentView(text)
        startForegroundService(Intent(this, NodeService::class.java))
    }
    @Suppress("DEPRECATION")
    override fun onStart() {
        super.onStart()
        val filter = IntentFilter(NodeService.UPDATE)
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(receiver, filter, RECEIVER_NOT_EXPORTED)
        else registerReceiver(receiver, filter)
    }
    override fun onStop() { unregisterReceiver(receiver); super.onStop() }
}

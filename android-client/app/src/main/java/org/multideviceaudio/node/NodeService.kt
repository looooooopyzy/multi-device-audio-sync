package org.multideviceaudio.node

import android.app.*
import android.content.Intent
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import java.net.*
import java.nio.ByteBuffer
import java.nio.channels.DatagramChannel
import java.nio.channels.SelectionKey
import java.nio.channels.Selector
import kotlin.concurrent.thread

class NodeService : Service() {
    companion object { const val UPDATE = "org.multideviceaudio.node.UPDATE" }
    @Volatile private var running = false
    private var worker: Thread? = null
    private var discoverySocket: DatagramChannel? = null
    private var clockSocket: DatagramChannel? = null
    private var selector: Selector? = null
    private var wifiLock: WifiManager.MulticastLock? = null

    override fun onBind(intent: Intent?): IBinder? = null
    override fun onCreate() {
        super.onCreate()
        val channel = NotificationChannel("node", "Audio node", NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        val notification = Notification.Builder(this, "node").setContentTitle("Audio Node")
            .setContentText("Listening for LAN master").setSmallIcon(android.R.drawable.ic_media_play).build()
        startForeground(1, notification)
        running = true
        worker = thread(name = "node-network") { runNetwork() }
    }
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int) = START_STICKY
    override fun onDestroy() {
        running = false; selector?.wakeup(); discoverySocket?.close(); clockSocket?.close();
        selector?.close(); wifiLock?.release()
        worker?.interrupt(); super.onDestroy()
    }
    private fun monoNs(): Long = System.nanoTime()
    private fun report(status: String, master: String = "-", offset: Double = 0.0,
                       rtt: Double = 0.0, drift: Double = 0.0, samples: Long = 0,
                       id: Long = 0) {
        sendBroadcast(Intent(UPDATE).setPackage(packageName).putExtra("status", status)
            .putExtra("master", master).putExtra("offset", offset).putExtra("rtt", rtt)
            .putExtra("drift", drift).putExtra("samples", samples).putExtra("id", id))
    }
    private fun send(socket: DatagramChannel, packet: WirePacket, target: SocketAddress) {
        val bytes = Wire.encode(packet)
        socket.send(ByteBuffer.wrap(bytes), target)
    }
    private fun runNetwork() {
        val id = DeviceIdentity.id(this)
        val name = (Build.MODEL ?: "Android").filter { it.code in 32..126 }.take(40).ifEmpty { "Android" }
        try {
            val wifi = applicationContext.getSystemService(WIFI_SERVICE) as WifiManager
            wifiLock = wifi.createMulticastLock("audio-node").apply { setReferenceCounted(false); acquire() }
            val listen = DatagramChannel.open().apply {
                setOption(StandardSocketOptions.SO_REUSEADDR, true)
                bind(InetSocketAddress(Wire.DISCOVERY)); configureBlocking(false)
            }
            val clock = DatagramChannel.open().apply {
                bind(InetSocketAddress(0)); configureBlocking(false)
            }
            val poller = Selector.open()
            listen.register(poller, SelectionKey.OP_READ)
            clock.register(poller, SelectionKey.OP_READ)
            selector = poller
            discoverySocket = listen; clockSocket = clock
            var master: InetSocketAddress? = null
            var lastSeen = 0L; var lastHeartbeat = 0L
            report("SEARCHING", id = id)
            while (running) {
                poller.select(100)
                val keys = poller.selectedKeys().iterator()
                while (keys.hasNext()) {
                    val key = keys.next(); keys.remove()
                    val socket = key.channel() as DatagramChannel
                    while (true) {
                        val buf = ByteBuffer.allocate(256)
                        val source = socket.receive(buf) ?: break
                        val t2 = monoNs()
                        val p = Wire.decode(buf.array(), buf.position()) ?: continue
                        val from = source as InetSocketAddress
                        if (socket === listen && p.type == 1) {
                            master = InetSocketAddress(from.address, Wire.SYNC)
                            lastSeen = t2
                            send(clock, WirePacket(2, p.sequence, id, platform = 2, name = name), from)
                            report("CONNECTED", from.address.hostAddress ?: "-", id = id)
                        } else if (socket === clock && p.type == 3 && master?.address == from.address && from.port == Wire.SYNC) {
                            lastSeen = t2
                            val t3 = monoNs()
                            send(clock, WirePacket(4, p.sequence, id, p.t1, t2, t3, 2, name), from)
                        } else if (socket === clock && p.type == 5 && p.platform == 1 && master?.address == from.address && from.port == Wire.SYNC) {
                            lastSeen = t2
                            report("CONNECTED", from.address.hostAddress ?: "-", p.t1 / 1e6,
                                p.t2 / 1e6, p.t3 / 1000.0, p.sequence, id)
                        }
                    }
                }
                val now = monoNs()
                if (master != null && now - lastHeartbeat >= 1_000_000_000L) {
                    send(clock, WirePacket(5, deviceId = id, platform = 2, name = name), master)
                    lastHeartbeat = now
                }
                if (master != null && now - lastSeen > 5_000_000_000L) {
                    report("TIMEOUT", master.address.hostAddress ?: "-", id = id); master = null
                }
            }
        } catch (e: Exception) { if (running) report("ERROR: ${e.message}", id = id) }
    }
}

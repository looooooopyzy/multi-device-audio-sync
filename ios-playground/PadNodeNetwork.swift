import Foundation
import Combine
import Network
import UIKit

@MainActor final class PadNodeState: ObservableObject {
    @Published var deviceName = UIDevice.current.name
    @Published var deviceId = "-"
    @Published var masterIP = "-"
    @Published var status = "ENTER MASTER IP"
    @Published var offsetMs = 0.0
    @Published var rttMs = 0.0
    @Published var driftPpm = 0.0
    @Published var samples: UInt32 = 0
}

final class PadNodeNetwork {
    private let state: PadNodeState
    private let queue = DispatchQueue(label: "org.multideviceaudio.pad.network")
    private var connection: NWConnection?
    private var timer: DispatchSourceTimer?
    private var lastMasterNs: Int64 = 0
    private let id: UInt64
    private let name: String

    init(state: PadNodeState) {
        self.state = state
        if let saved = UserDefaults.standard.string(forKey: "padDeviceId"),
           let value = UInt64(saved), value != 0 {
            id = value
        } else {
            id = UInt64.random(in: 1...UInt64.max)
            UserDefaults.standard.set(String(id), forKey: "padDeviceId")
        }
        var safeName = ""
        for scalar in UIDevice.current.name.unicodeScalars {
            let next = safeName + String(scalar)
            if next.utf8.count > 48 { break }
            safeName = next
        }
        name = safeName.isEmpty ? "iPad" : safeName
        Task { @MainActor in state.deviceId = String(id) }
    }

    func connect(to ip: String) {
        guard IPv4Address(ip) != nil else {
            setStatus("INVALID IPv4 ADDRESS")
            return
        }
        queue.async { [self] in
            connection?.cancel()
            connection = nil
            timer?.cancel()
            timer = nil
            lastMasterNs = MonotonicClock.nowNs()
            let link = NWConnection(host: NWEndpoint.Host(ip),
                                    port: NWEndpoint.Port(rawValue: Wire.syncPort)!,
                                    using: .udp)
            connection = link
            link.stateUpdateHandler = { [weak self, weak link] status in
                guard let self, let link, self.connection === link else { return }
                switch status {
                case .ready:
                    self.setStatus("WAITING FOR MASTER")
                    self.announce(on: link)
                    self.receive(on: link)
                    let timer = DispatchSource.makeTimerSource(queue: self.queue)
                    timer.schedule(deadline: .now() + 1, repeating: 1)
                    timer.setEventHandler { [weak self, weak link] in
                        guard let self, let link, self.connection === link else { return }
                        self.announce(on: link)
                        self.send(WirePacket(type: 5, deviceId: self.id,
                                             platform: 4, name: self.name), on: link)
                        if MonotonicClock.nowNs() - self.lastMasterNs > 5_000_000_000 {
                            self.setStatus("TIMEOUT / CHECK MASTER")
                        }
                    }
                    timer.resume()
                    self.timer = timer
                    Task { @MainActor in self.state.masterIP = ip }
                case .failed(let error):
                    self.setStatus("ERROR: \(error)")
                default: break
                }
            }
            link.start(queue: queue)
        }
    }

    func stop() {
        queue.async { [self] in
            timer?.cancel(); timer = nil
            connection?.cancel(); connection = nil
        }
    }

    private func announce(on link: NWConnection) {
        // Windows master accepts DISCOVERY_RESPONSE on its sync port too.
        // This gives the iPad a unicast-only path without multicast entitlement.
        send(WirePacket(type: 2, deviceId: id, platform: 4, name: name), on: link)
    }

    private func receive(on link: NWConnection) {
        link.receiveMessage { [weak self, weak link] data, _, _, error in
            guard let self, let link, self.connection === link else { return }
            let t2 = MonotonicClock.nowNs()
            if let data, let packet = Wire.decode(data), packet.platform == 1 {
                switch packet.type {
                case 3:
                    self.lastMasterNs = t2
                    let t3 = MonotonicClock.nowNs()
                    self.send(WirePacket(type: 4, sequence: packet.sequence,
                                         deviceId: self.id, t1: packet.t1,
                                         t2: t2, t3: t3, platform: 4,
                                         name: self.name), on: link)
                case 5:
                    self.lastMasterNs = t2
                    Task { @MainActor in
                        self.state.offsetMs = Double(packet.t1) / 1e6
                        self.state.rttMs = Double(packet.t2) / 1e6
                        self.state.driftPpm = Double(packet.t3) / 1000
                        self.state.samples = packet.sequence
                        self.state.status = "CONNECTED"
                    }
                default: break
                }
            }
            if let error { self.setStatus("ERROR: \(error)") }
            else { self.receive(on: link) }
        }
    }

    private func send(_ packet: WirePacket, on link: NWConnection) {
        guard let data = Wire.encode(packet) else { return }
        link.send(content: data, completion: .contentProcessed { [weak self] error in
            if let error { self?.setStatus("ERROR: \(error)") }
        })
    }

    private func setStatus(_ text: String) {
        Task { @MainActor in state.status = text }
    }
}

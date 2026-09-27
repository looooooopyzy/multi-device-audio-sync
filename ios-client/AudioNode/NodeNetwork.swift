import Foundation
import Combine
import Network
import UIKit
import Darwin

@MainActor final class NodeState: ObservableObject {
    @Published var deviceName = UIDevice.current.name
    @Published var deviceId = "-"
    @Published var localIP = "-"
    @Published var masterIP = "-"
    @Published var status = "SEARCHING"
    @Published var offsetMs = 0.0
    @Published var rttMs = 0.0
    @Published var driftPpm = 0.0
    @Published var samples: UInt32 = 0
}

final class NodeNetwork {
    let state: NodeState
    private let queue = DispatchQueue(label: "org.multideviceaudio.node.network")
    private var discoverySource: DispatchSourceRead?
    private var master: NWConnection?
    private var masterHost: NWEndpoint.Host?
    private var manualHost: NWEndpoint.Host?
    private var heartbeat: DispatchSourceTimer?
    private var lastMasterNs: Int64 = 0
    private let id: UInt64
    private let name: String
    private let platform: UInt8

    init(state: NodeState) {
        self.state = state
        if let saved = UserDefaults.standard.string(forKey: "deviceId"), let value = UInt64(saved), value != 0 {
            id = value
        } else {
            let value = UInt64.random(in: 1...UInt64.max)
            id = value
            UserDefaults.standard.set(String(value), forKey: "deviceId")
        }
        var safeName = ""
        for scalar in UIDevice.current.name.unicodeScalars {
            let next = safeName + String(scalar)
            if next.utf8.count > 48 { break }
            safeName = next
        }
        name = safeName.isEmpty ? UIDevice.current.model : safeName
        platform = UIDevice.current.userInterfaceIdiom == .pad ? 4 : 3
        Task { @MainActor in state.deviceId = String(id); state.localIP = Self.localIPv4() }
    }
    func start() {
        queue.async { [self] in
            do {
                try self.startDiscoverySocket()
            } catch { setStatus("DISCOVERY UNAVAILABLE - ENTER MASTER IP") }
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now() + 1, repeating: 1)
            timer.setEventHandler { [weak self] in self?.tick() }
            timer.resume(); heartbeat = timer
        }
    }
    func stop() {
        queue.async { [self] in
            heartbeat?.cancel(); heartbeat = nil
            discoverySource?.cancel(); discoverySource = nil
            master?.cancel(); master = nil
        }
    }
    func connectManually(ip: String) {
        guard IPv4Address(ip) != nil else { setStatus("INVALID IPv4 ADDRESS"); return }
        queue.async { [self] in
            let host = NWEndpoint.Host(ip)
            manualHost = host
            lastMasterNs = MonotonicClock.nowNs()
            connectMaster(host: host, discoverySequence: 0)
        }
    }
    private func startDiscoverySocket() throws {
        let fd = Darwin.socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { throw NSError(domain: "AudioNode UDP socket", code: Int(errno)) }
        var reuse: Int32 = 1
        var address = sockaddr_in()
        address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        address.sin_family = sa_family_t(AF_INET)
        address.sin_port = Wire.discoveryPort.bigEndian
        address.sin_addr = in_addr(s_addr: INADDR_ANY)
        let reuseResult = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                                     socklen_t(MemoryLayout.size(ofValue: reuse)))
        guard reuseResult == 0 else {
            let failure = errno; _ = Darwin.close(fd)
            throw NSError(domain: "AudioNode UDP reuse", code: Int(failure))
        }
        let bindResult = withUnsafePointer(to: &address) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0, fcntl(fd, F_SETFL, O_NONBLOCK) == 0 else {
            let failure = errno; _ = Darwin.close(fd)
            throw NSError(domain: "AudioNode UDP bind", code: Int(failure))
        }
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.receiveDiscovery(fd: fd) }
        source.setCancelHandler { _ = Darwin.close(fd) }
        source.resume()
        discoverySource = source
    }
    private func receiveDiscovery(fd: Int32) {
        while true {
            var bytes = [UInt8](repeating: 0, count: 256)
            var peer = sockaddr_in()
            var peerLength = socklen_t(MemoryLayout<sockaddr_in>.size)
            let count = bytes.withUnsafeMutableBytes { buffer in
                withUnsafeMutablePointer(to: &peer) { pointer in
                    pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                        Darwin.recvfrom(fd, buffer.baseAddress, buffer.count, 0, $0, &peerLength)
                    }
                }
            }
            let receivedAt = MonotonicClock.nowNs()
            if count < 0 {
                if errno != EAGAIN && errno != EWOULDBLOCK { setStatus("ERROR: discovery receive \(errno)") }
                return
            }
            guard peer.sin_family == sa_family_t(AF_INET),
                  let packet = Wire.decode(Data(bytes.prefix(Int(count)))),
                  packet.type == 1, packet.platform == 1 else { continue }
            if manualHost != nil { continue }
            var ipv4 = peer.sin_addr
            var hostBytes = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
            guard inet_ntop(AF_INET, &ipv4, &hostBytes, socklen_t(hostBytes.count)) != nil else { continue }
            lastMasterNs = receivedAt
            connectMaster(host: NWEndpoint.Host(String(cString: hostBytes)),
                          discoverySequence: packet.sequence)
        }
    }
    private func connectMaster(host: NWEndpoint.Host, discoverySequence: UInt32) {
        if masterHost == host, let master {
            send(WirePacket(type: 2, sequence: discoverySequence, deviceId: id,
                            platform: platform, name: name), on: master)
            return
        }
        master?.cancel()
        masterHost = host
        let connection = NWConnection(host: host, port: NWEndpoint.Port(rawValue: Wire.syncPort)!, using: .udp)
        master = connection
        connection.stateUpdateHandler = { [weak self] status in
            guard let self else { return }
            switch status {
            case .ready:
                self.send(WirePacket(type: 2, sequence: discoverySequence, deviceId: self.id,
                                     platform: self.platform, name: self.name), on: connection)
                self.receiveMaster(connection)
                Task { @MainActor in self.state.masterIP = "\(host)"; self.state.status = "WAITING FOR MASTER" }
            case .failed(let error): self.setStatus("ERROR: \(error)")
            default: break
            }
        }
        connection.start(queue: queue)
    }
    private func receiveMaster(_ connection: NWConnection) {
        connection.receiveMessage { [weak self] data, _, _, error in
            guard let self else { return }
            let t2 = MonotonicClock.nowNs()
            if let data, let p = Wire.decode(data), p.platform == 1 {
                if p.type == 3 {
                    self.lastMasterNs = t2
                    let t3 = MonotonicClock.nowNs()
                    self.send(WirePacket(type: 4, sequence: p.sequence, deviceId: self.id,
                                         t1: p.t1, t2: t2, t3: t3, platform: self.platform,
                                         name: self.name), on: connection)
                } else if p.type == 5 {
                    self.lastMasterNs = t2
                    Task { @MainActor in
                        self.state.offsetMs = Double(p.t1) / 1e6
                        self.state.rttMs = Double(p.t2) / 1e6
                        self.state.driftPpm = Double(p.t3) / 1000
                        self.state.samples = p.sequence
                        self.state.status = "CONNECTED"
                    }
                }
            }
            if error == nil { self.receiveMaster(connection) }
        }
    }
    private func send(_ p: WirePacket, on connection: NWConnection) {
        guard let data = Wire.encode(p) else { return }
        connection.send(content: data, completion: .contentProcessed { [weak self] error in
            if let error { self?.setStatus("ERROR: \(error)") }
        })
    }
    private func tick() {
        let now = MonotonicClock.nowNs()
        if let master {
            if manualHost != nil {
                send(WirePacket(type: 2, deviceId: id, platform: platform, name: name), on: master)
            }
            send(WirePacket(type: 5, deviceId: id, platform: platform, name: name), on: master)
            if now - lastMasterNs > 5_000_000_000 {
                setStatus("TIMEOUT")
                master.cancel(); self.master = nil; masterHost = nil
            }
        } else if let manualHost {
            lastMasterNs = now
            connectMaster(host: manualHost, discoverySequence: 0)
        }
    }
    private func setStatus(_ text: String) { Task { @MainActor in state.status = text } }
    private static func localIPv4() -> String {
        var address: String = "-"
        var interfaces: UnsafeMutablePointer<ifaddrs>?
        if getifaddrs(&interfaces) == 0 {
            var cursor = interfaces
            while let item = cursor {
                let entry = item.pointee
                if let addr = entry.ifa_addr, addr.pointee.sa_family == UInt8(AF_INET),
                   String(cString: entry.ifa_name) == "en0" {
                    var buffer = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                    if getnameinfo(addr, socklen_t(addr.pointee.sa_len), &buffer,
                                   socklen_t(buffer.count), nil, 0, NI_NUMERICHOST) == 0 {
                        address = String(cString: buffer)
                    }
                }
                cursor = entry.ifa_next
            }
            freeifaddrs(interfaces)
        }
        return address
    }
}

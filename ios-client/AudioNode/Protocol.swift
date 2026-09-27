import Foundation

struct WirePacket {
    let type: UInt16
    var sequence: UInt32 = 0
    var deviceId: UInt64 = 0
    var t1: Int64 = 0
    var t2: Int64 = 0
    var t3: Int64 = 0
    var platform: UInt8 = 3
    var name: String = ""
}

enum Wire {
    static let size = 96
    static let discoveryPort: UInt16 = 45670
    static let syncPort: UInt16 = 45671

    static func encode(_ p: WirePacket) -> Data? {
        let name = Array(p.name.utf8)
        guard (1...5).contains(p.type), name.count <= 48, !name.contains(0) else { return nil }
        var b = [UInt8](repeating: 0, count: size)
        func put(_ value: UInt64, at index: Int, bytes: Int) {
            for i in 0..<bytes { b[index+i] = UInt8(truncatingIfNeeded: value >> ((bytes-1-i)*8)) }
        }
        put(0x4d444131, at: 0, bytes: 4)
        put(1, at: 4, bytes: 2)
        put(UInt64(p.type), at: 6, bytes: 2)
        put(UInt64(p.sequence), at: 8, bytes: 4)
        put(p.deviceId, at: 12, bytes: 8)
        put(UInt64(bitPattern: p.t1), at: 20, bytes: 8)
        put(UInt64(bitPattern: p.t2), at: 28, bytes: 8)
        put(UInt64(bitPattern: p.t3), at: 36, bytes: 8)
        b[44] = p.platform; b[45] = UInt8(name.count)
        for (i, value) in name.enumerated() { b[48+i] = value }
        return Data(b)
    }
    static func decode(_ data: Data) -> WirePacket? {
        let b = Array(data)
        guard b.count == size else { return nil }
        func get(_ index: Int, _ count: Int) -> UInt64 {
            var value: UInt64 = 0
            for i in 0..<count { value = (value << 8) | UInt64(b[index+i]) }
            return value
        }
        guard get(0,4) == 0x4d444131, get(4,2) == 1,
              (1...5).contains(get(6,2)), (1...5).contains(b[44]), b[45] <= 48,
              b[46] == 0, b[47] == 0 else { return nil }
        let n = Int(b[45]); let nameBytes = Array(b[48..<(48+n)])
        guard !nameBytes.contains(0), b[(48+n)..<size].allSatisfy({ $0 == 0 }),
              let name = String(bytes: nameBytes, encoding: .utf8) else { return nil }
        return WirePacket(type: UInt16(get(6,2)), sequence: UInt32(get(8,4)),
                          deviceId: get(12,8), t1: Int64(bitPattern: get(20,8)),
                          t2: Int64(bitPattern: get(28,8)), t3: Int64(bitPattern: get(36,8)),
                          platform: b[44], name: name)
    }
}

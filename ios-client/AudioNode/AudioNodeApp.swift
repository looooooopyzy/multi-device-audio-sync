import SwiftUI

@main struct AudioNodeApp: App {
    @StateObject private var state = NodeState()
    @State private var network: NodeNetwork?
    @State private var manualIP = UserDefaults.standard.string(forKey: "masterIPv4") ?? ""
    var body: some Scene {
        WindowGroup {
            VStack(alignment: .leading, spacing: 12) {
                Text("Audio Node").font(.largeTitle).bold()
                HStack {
                    TextField("Windows master IPv4", text: $manualIP)
                        .textFieldStyle(.roundedBorder)
                        .keyboardType(.numbersAndPunctuation)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("Connect") {
                        UserDefaults.standard.set(manualIP, forKey: "masterIPv4")
                        network?.connectManually(ip: manualIP.trimmingCharacters(in: .whitespacesAndNewlines))
                    }
                    .buttonStyle(.borderedProminent)
                }
                Text("Automatic discovery needs Apple's multicast entitlement. Manual IP uses unicast.")
                    .font(.footnote).foregroundColor(.secondary)
                Group {
                    row("Device Name", state.deviceName)
                    row("Device ID", state.deviceId)
                    row("Local IP", state.localIP)
                    row("Master IP", state.masterIP)
                    row("Connection Status", state.status)
                    row("Clock Offset", String(format: "%+.3f ms", state.offsetMs))
                    row("RTT", String(format: "%.3f ms", state.rttMs))
                    row("Drift", String(format: "%+.2f ppm", state.driftPpm))
                    row("Samples", String(state.samples))
                }
                Spacer()
            }
            .padding()
            .onAppear { if network == nil { let node = NodeNetwork(state: state); network = node; node.start() } }
            .onDisappear { network?.stop(); network = nil }
        }
    }
    private func row(_ label: String, _ value: String) -> some View {
        HStack(alignment: .top) { Text(label).frame(width: 155, alignment: .leading).foregroundColor(.secondary)
            Text(value).textSelection(.enabled) }
    }
}

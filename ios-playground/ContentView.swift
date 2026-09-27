import SwiftUI

// Replace the starter ContentView.swift in a new iPad Swift Playgrounds App
// with this file. The app template keeps its own @main App file.
struct ContentView: View {
    @StateObject private var state = PadNodeState()
    @State private var masterIP = UserDefaults.standard.string(forKey: "padMasterIP") ?? ""
    @State private var network: PadNodeNetwork?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Text("Audio Node · iPad").font(.largeTitle).bold()
                Text("Clock sync test over Wi-Fi (manual master IP)")
                    .foregroundColor(.secondary)

                HStack {
                    TextField("Windows master IPv4, e.g. 192.168.1.20", text: $masterIP)
                        .textFieldStyle(.roundedBorder)
                        .keyboardType(.numbersAndPunctuation)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    Button("Connect") {
                        UserDefaults.standard.set(masterIP, forKey: "padMasterIP")
                        if network == nil { network = PadNodeNetwork(state: state) }
                        network?.connect(to: masterIP.trimmingCharacters(in: .whitespacesAndNewlines))
                    }
                    .buttonStyle(.borderedProminent)
                }

                row("Device", state.deviceName)
                row("Device ID", state.deviceId)
                row("Master", state.masterIP)
                row("Status", state.status)
                row("Offset", String(format: "%+.3f ms", state.offsetMs))
                row("RTT", String(format: "%.3f ms", state.rttMs))
                row("Drift", String(format: "%+.2f ppm", state.driftPpm))
                row("Samples", String(state.samples))
                Text("Keep this app in the foreground during the test.")
                    .foregroundColor(.secondary)
            }
            .padding()
        }
        .onDisappear { network?.stop(); network = nil }
    }

    private func row(_ title: String, _ value: String) -> some View {
        HStack(alignment: .top) {
            Text(title).frame(width: 110, alignment: .leading).foregroundColor(.secondary)
            Text(value).textSelection(.enabled)
            Spacer(minLength: 0)
        }
    }
}

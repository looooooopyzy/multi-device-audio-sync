# Physical Android / iOS verification runbook

> Legacy native UDP path. For the current browser speaker and Phase B PCM stream, use [Browser Speaker Client](web-client.md). The native Android/iOS clients and Codemagic path are retained but paused.

This milestone measures network clock mapping. A successful run does not establish actual acoustic output alignment.

## 1. LAN and Windows master

Connect Windows, Android and iPhone/iPad to the same local Wi-Fi/LAN broadcast domain. Avoid guest networks, AP/client isolation and VPNs. Check that the devices receive addresses on the expected LAN. On Windows, set the network profile to Private. The master receives discovery replies on an ephemeral UDP port as well as 45671, so permit inbound UDP for the **executable**, not only port 45671:

```powershell
# Run once in an elevated PowerShell from the repository root.
$masterExe = (Resolve-Path .\build\windows-master\windows-master.exe).Path
New-NetFirewallRule -DisplayName "MDA Master UDP" -Direction Inbound -Action Allow -Protocol UDP -Profile Private -Program $masterExe
```

Run from the repository root:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\windows-master\windows-master.exe
```

Discovery uses UDP 45670 each second; sync uses UDP 45671 four times per second per node. The master prints a table each second. If `Ninja` is not installed, use the generator supplied by Visual Studio instead.

## 2. Android

Open `android-client/` in Android Studio, connect an Android 8+ phone by USB, enable USB debugging and authorize the computer. Build and Run `app` in Android Studio. The Windows-built APK can also be installed with:

```powershell
adb devices
adb install -r "android-client\app\build\outputs\apk\debug\app-debug.apk"
adb shell am start -n org.multideviceaudio.node/.MainActivity
```

Keep Wi-Fi enabled. The app should show device ID, local IP, SEARCHING, then CONNECTED and the master IP. Its foreground service runs independently of the visible screen; notification visibility depends on device notification settings. The project targets SDK 35 and declares INTERNET; Android's new `ACCESS_LOCAL_NETWORK` runtime permission applies when targeting SDK 37+, so it is not requested here. [Android local network permission documentation](https://developer.android.com/privacy-and-security/local-network-permission).

## 3. iPhone / iPad

If using Codemagic as the remote Mac builder, follow the [Codemagic → iPad runbook](codemagic-ipad.md) for the unsigned compile check, Ad Hoc signing, installation and manual-IP real-device sync test.

Use a Mac with Xcode to open `ios-client/AudioNode.xcodeproj`. Select the `AudioNode` target, set a signing team and a unique bundle identifier, and select the physical device. This project's discovery uses UDP broadcast, which requires Apple's approved `com.apple.developer.networking.multicast` entitlement on physical iOS/iPadOS. After approval, configure `AudioNode/MulticastNetworking.entitlements` as Code Signing Entitlements and regenerate/select a compatible provisioning profile. [Apple entitlement documentation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.networking.multicast).

If you have an iPad but no Mac or remote Mac build channel, use the [Swift Playgrounds iPad runbook](../ios-playground/README.md). It runs a real-device **unicast** sync test after you enter the Windows master's IPv4 address. This bypasses the app's broadcast discovery, so treat those as separate acceptance checks. If a remote macOS build channel becomes available later, `.github/workflows/ios-build.yml` can compile the Simulator target without signing; a green CI build is not a physical-device or LAN check.

Run the app in the foreground and allow the Local Network prompt. If previously denied, use Settings > Privacy & Security > Local Network to enable it. The Xcode project already supplies `NSLocalNetworkUsageDescription`. The discovery listener uses a BSD UDP socket; the sync socket uses Network.framework. iOS background suspension prevents continuous sync in this milestone. **IMPLEMENTED BUT NOT BUILD-VERIFIED** on this Windows host.

## 4. Acceptance observations

1. Within several seconds, the master shows both physical nodes with their LAN IP and ONLINE status.
2. Sample counts grow by roughly four per second while each app remains active; the device UI updates from master telemetry. Record values after at least 2–3 minutes: raw/filtered RTT, offset, jitter, drift ppm, quality and last seen.
3. Offset is **client minus master monotonic time**. Absolute offset need not be near zero across different devices, since the monotonic origins differ. Check that its slope settles and that drift does not jump wildly once enough samples accumulate.
4. Turn one device's Wi-Fi off or stop its process: master status should become TIMEOUT after about 5 seconds. Restore connectivity and check that it returns ONLINE and samples resume.
5. If discovery fails, inspect `udp.port == 45670 || udp.port == 45671` in Wireshark on Windows. Verify broadcasts leave the physical adapter, a discovery response reaches the master, and sync request/response pairs follow. Virtual adapters may cause the master to show an unexpected source IP.

No fixed RTT threshold proves acoustic synchronization. Actual 1–5 ms output alignment must be measured after the audio-clock/playback milestone.

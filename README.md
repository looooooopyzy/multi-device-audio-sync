# Multi-device Audio Sync / 多设备音频同步

Synchronize Windows music with browser speakers on phones, tablets, and computers over a local network. The project includes a Windows master, a browser client for iPad Safari and Android Chrome, and acoustic delay calibration.

通过局域网让 Windows 正在播放的音乐与手机、平板和电脑浏览器同步。项目包含 Windows 主控、适用于 iPad Safari 和 Android Chrome 的浏览器扬声器，以及声学延迟校准。

> **Project status / 项目状态:** The Windows master and iPad Pro (2021) Safari music playback have been verified by the maintainer. Android Chrome is supported by the browser client but still needs physical-device verification. The spatial mode assigns stereo channels by physical speaker position; it is not an object-audio or Dolby Atmos renderer.
>
> Windows 主控与 iPad Pro（2021）Safari 音乐同步已由维护者实机验证。Android Chrome 可使用浏览器客户端，但仍需真机验证。空间模式按实体扬声器位置分配立体声声道，并非对象音频或杜比全景声渲染。

## Features / 功能

- Capture the Windows default playback mix with WASAPI loopback.
- Send timestamped 48 kHz stereo PCM to browser clients over WebSocket.
- Schedule Windows and browser playback against a shared clock, with adaptive network buffering.
- Calibrate each browser speaker acoustically using a physical Windows microphone.
- Use an optional virtual playback device to route computer music through the master while keeping a separate physical speaker output.
- Retain the original UDP synchronization code and experimental native Android/iOS clients.
- Switch between the original full-stereo sync mode and a room-layout spatial mode without changing the capture stream or acoustic calibration.

- 使用 WASAPI Loopback 捕获 Windows 默认播放混音。
- 通过 WebSocket 向浏览器客户端发送带时间戳的 48 kHz 立体声 PCM。
- 使用统一时钟排程 Windows 与浏览器播放，并根据网络状况调整缓冲。
- 使用 Windows 实体麦克风对各浏览器扬声器进行声学校准。
- 可选使用虚拟播放设备，将电脑音乐交给主控处理，再输出到独立的实体扬声器。
- 保留原有 UDP 同步代码，以及实验性的 Android/iOS 原生客户端。
- 可在原有完整立体声同步模式与房间布局空间模式之间切换，继续使用同一音源和声学校准。

## Playback modes / 播放模式

**Mode 1 — Normal sync:** Every active speaker plays the complete stereo stream. This is the original behavior and remains the default on first launch.

**Mode 2 — Spatial audio:** Open the browser speaker page and choose **Spatial audio**. Drag the Windows speaker and each connected browser speaker on the room map to match their positions relative to the listening point. The leftmost active speaker mainly plays the left channel; the rightmost mainly plays the right channel. Speakers between them receive a weighted mix. The vertical position is recorded for the room diagram but does not yet change audio. Fewer than two active speakers, or speakers with nearly identical horizontal positions, keep full stereo. Mode and positions are saved locally by the Windows master. After moving a physical speaker or changing the listening point, run acoustic calibration again.

**版本 1——普通同步：**各扬声器仍播放完整立体声。这是原来的播放方式，首次启动默认使用它。

**版本 2——空间音频：**在浏览器扬声器页面选择“空间音频”，按实际方位拖动房间图中的 Windows 扬声器和已连接设备。最左侧设备主要播放左声道，最右侧主要播放右声道，中间设备按位置混合。纵向位置目前只记录布局，不改变声音。少于两台有效扬声器，或设备左右位置过近时，仍播放完整立体声。模式与位置由 Windows 主控保存在本地。移动实体设备或改变听音位置后，请重新进行声音自动校准。

## Quick start: Windows / Windows 快速开始

### Requirements / 环境要求

- Windows 10 or 11, x64.
- CMake 3.20 or later, Ninja, and a C++17 Windows toolchain (MinGW-w64 or Visual C++).
- Python 3.10 or later for the HTTPS helper used by the graphical calibrator.

- Windows 10 或 11，x64。
- CMake 3.20 或更新版本、Ninja，以及支持 C++17 的 Windows 工具链（MinGW-w64 或 Visual C++）。
- Python 3.10 或更新版本，用于图形校准程序的 HTTPS 服务。

### Build / 构建

```powershell
python -m pip install -r tools/requirements.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Play music on Windows and browser speakers / 同步播放电脑音乐

1. Install [VB-CABLE](https://vb-audio.com/Cable/) or another supported virtual playback endpoint.
2. In Windows sound settings, set **CABLE Input (VB-Audio Virtual Cable)** as the default playback device.
3. Run `build\windows-master\windows-calibrator.exe`. The graphical app starts in computer-music mode and starts the local HTTP/HTTPS services.
4. In **Computer speaker output**, select a physical speaker. The app forwards the same captured PCM to that speaker and connected browsers.
5. Open the calibration URL shown by the app on each device. On iPad, use the first-time setup URL to install and trust the local certificate, then open the HTTPS calibration URL. On Android music playback, Chrome can open the HTTP URL directly; Android microphone access is not needed.
6. On each device, tap **Enable Speaker**. Confirm the audio source shows **System music (synchronized)** and the received packet count increases.
7. To calibrate a new device, keep already-synchronized browser pages active, pause the music briefly, and tap **Two-way acoustic calibration** on the new device. The shared Windows delay stays fixed while the new device's delay is adjusted.

The GUI remaps the selected physical speaker and reconnects when the Windows default output changes. Wait for **Synchronized playback ready** before expecting sound from the PC speaker while CABLE Input is the default.

1. 安装 [VB-CABLE](https://vb-audio.com/Cable/) 或其他受支持的虚拟播放端点。
2. 在 Windows 声音设置中，将 **CABLE Input (VB-Audio Virtual Cable)** 设为默认播放设备。
3. 运行 `build\windows-master\windows-calibrator.exe`。图形程序默认进入电脑音乐模式，并启动本地 HTTP/HTTPS 服务。
4. 在“电脑扬声器输出”中选择实体扬声器。程序会把捕获到的同一份 PCM 转发到该扬声器和已连接的浏览器。
5. 在各设备打开程序显示的校准地址。iPad 首次使用时，通过首次设置地址安装并信任本地证书，再打开 HTTPS 校准地址。Android 播放音乐时可直接用 Chrome 打开 HTTP 地址，无需 Android 麦克风权限。
6. 在各设备点击“启用扬声器”。确认音源显示“系统音乐（双端同步）”，且接收音频包数量持续增加。
7. 校准新设备时，保持已同步的浏览器页面运行，暂时暂停音乐，然后在新设备点击“双向声音自动校准”。程序保持 Windows 公共延迟不变，只调整新设备的延迟。

切换 Windows 默认输出后，图形程序会重新匹配所选实体扬声器并自动重连。将 CABLE Input 设为默认输出后，请等状态显示“**双端受控播放已就绪**”，电脑扬声器才会由程序转送声音。

Keep mobile browsers in the foreground during playback and calibration. The Windows microphone must be a physical microphone that can hear the Windows speaker and the device being calibrated. The GUI also supports a fixed test tone mode.

播放和校准时请让手机浏览器保持在前台。Windows 麦克风必须是能同时听到电脑扬声器和待校准设备的实体麦克风。图形程序也支持固定测试音模式。

## Command-line mode / 命令行模式

Run a browser test tone:

```powershell
.\build\windows-master\windows-master.exe --source tone --web-only
```

Capture the Windows default playback mix and forward it to browser clients:

```powershell
.\build\windows-master\windows-master.exe --source system --web-only
```

For synchronized local and browser playback, use a virtual endpoint as the Windows default output and specify a different physical render device:

```powershell
.\build\windows-master\windows-master.exe --source system --local-stream-output --render-device 1 --web-only
```

Run the existing C++ and Python integration tests with:

```powershell
ctest --test-dir build --output-on-failure
```

播放浏览器测试音：

```powershell
.\build\windows-master\windows-master.exe --source tone --web-only
```

捕获 Windows 默认播放混音并转发到浏览器：

```powershell
.\build\windows-master\windows-master.exe --source system --web-only
```

要让电脑实体扬声器和浏览器同步播放，请将虚拟设备设为 Windows 默认输出，并指定另一个实体渲染设备：

```powershell
.\build\windows-master\windows-master.exe --source system --local-stream-output --render-device 1 --web-only
```

运行现有 C++ 和 Python 集成测试：

```powershell
ctest --test-dir build --output-on-failure
```

The command-line master prints its local-network URL. Allow the chosen TCP port through Windows Firewall on a trusted private network. The GUI selects free HTTP and HTTPS ports automatically. Do not expose these services to the public internet.

命令行主控会打印局域网访问地址。请在可信的专用网络中允许 Windows 防火墙放行所选 TCP 端口。图形程序会自动选择空闲 HTTP 和 HTTPS 端口。请勿将这些服务暴露到公网。

## Architecture / 架构

- `windows-master/`: Windows GUI launcher, WASAPI loopback capture, physical-device playback, calibration, and HTTP/WebSocket master.
- `web-client/`: browser speaker interface using Web Audio scheduling and PCM jitter buffering.
- `shared/`: clock synchronization, UDP protocol, PCM packet format, and acoustic-probe analysis.
- `android-client/`, `ios-client/`, `ios-playground/`: retained native experiments; the native Android client does not currently play the synchronized music stream.
- `tests/`: protocol, web, HTTPS, and audio-stream integration checks.
- `docs/`: architecture, calibration, protocol, and setup notes.

- `windows-master/`：Windows 图形启动器、WASAPI Loopback 捕获、实体设备播放、校准，以及 HTTP/WebSocket 主控。
- `web-client/`：使用 Web Audio 排程和 PCM 抖动缓冲的浏览器扬声器界面。
- `shared/`：时钟同步、UDP 协议、PCM 数据包格式和声学校准信号分析。
- `android-client/`、`ios-client/`、`ios-playground/`：保留的原生客户端实验；当前 Android 原生客户端尚不能播放同步音乐流。
- `tests/`：协议、网页、HTTPS 和音频流集成检查。
- `docs/`：架构、校准、协议和配置说明。

## Limitations / 当前限制

- Audio is stereo PCM. The spatial mode is position-based channel distribution around one listening point; multichannel object rendering and per-application capture are not implemented.
- System capture records the Windows default playback endpoint mix.
- Browser pages must remain foregrounded for reliable mobile playback.
- Android Chrome playback is implemented in the shared browser client, but Android acoustic synchronization still needs physical-device verification.
- Native iOS development and Codemagic IPA signing are paused.

- 当前传输为立体声 PCM；空间模式围绕一个听音位置分配声道，尚未实现多声道对象音频渲染或按应用捕获。
- 系统捕获读取 Windows 默认播放端点的混音。
- 为保证移动端稳定播放，浏览器页面需要保持在前台。
- Android Chrome 可使用通用浏览器客户端，但 Android 真机声学同步仍需验证。
- 原生 iOS 开发与 Codemagic IPA 签名目前暂停维护。

## License / 许可证

This project is released under the MIT License. See [LICENSE](LICENSE).

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

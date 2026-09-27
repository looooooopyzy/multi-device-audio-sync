# 仅使用 iPad 的时钟同步验证（无需 Mac）

> **Legacy / Experimental：** 这是保留的原生 UDP 真机实验路线。当前主要客户端是 [Safari 浏览器扬声器](../web-client/README.md)，不需要执行本页步骤。

本目录是一组供 Swift Playgrounds **App** 项目使用的源码。它通过手动输入 Windows Master 的 IPv4 地址进行 UDP 单播连接。Master 会在 UDP 45671 接收 `DISCOVERY_RESPONSE`，再向同一个 iPad UDP 端点发送时钟同步请求。此方法用于验证 iPad 真机时钟和 Wi-Fi 路径，不更改原有 UDP 协议。

## 在 Windows 上准备

1. 让 Windows 和 iPad 连接到同一 Wi-Fi/局域网。在 PowerShell 运行 `ipconfig`，记下当前 Wi-Fi 或以太网适配器的 IPv4 地址，不要使用 VPN 或虚拟网卡地址。
2. 从仓库根目录启动 Windows Master：

   ```powershell
   .\build\windows-master\windows-master.exe
   ```

3. 将 Windows 网络设为“专用”，并允许 Master 程序接收 UDP。可参考[真机验证文档](../docs/device-verification.md)中的管理员 PowerShell 防火墙命令。手动 iPad 测试会发送到 UDP 45671。

## 在 iPad 上准备

1. 从 App Store 安装 **Swift Playgrounds**，新建 **App** 项目，不要选择 Playground Book。
2. 用本目录的 `ContentView.swift` 完整替换模板中的同名文件。
3. 将 `PadNodeNetwork.swift`、`Protocol.swift` 和 `MonotonicClock.swift` 加入同一个 App 模块。可通过“文件”、iCloud Drive 或其他方式传输四个源码文件，并使用 Swift Playgrounds 侧边栏的 **Insert from** 导入。保留模板生成的 `@main` App 文件，使它显示 `ContentView()`。
4. 在 **App Settings → Capabilities** 中，为 **Local Network** 填写用途说明，例如“连接 Windows 音频主控以同步时钟”。点击运行，并在 iPadOS 提示时允许局域网访问。测试期间让 App 保持在前台。
5. 输入 Windows 局域网 IPv4 地址，点击 **Connect**，观察 Master 控制台。几秒后应出现状态为 `ONLINE` 的 iPad 节点；iPad 上应显示 `CONNECTED`，Samples 约每秒增加四次。

至少运行两分钟，记录 Master 中的 RTT、过滤后 RTT、偏移、抖动、漂移、采样数和同步质量。关闭 iPad Wi-Fi 后，Windows 约五秒后应显示 `TIMEOUT`。重新打开 Wi-Fi，必要时再次点击 **Connect**，确认采样恢复增长。

## 验证范围

这条路线只验证 **iPad 真机 UDP 单播时钟同步**，不验证 UDP 广播自动发现。iOS/iPadOS 真机广播通常需要 Apple 批准的组播网络权限。本路线也不验证扬声器实际发声时间是否对齐；App 在后台时可能被 iPadOS 暂停。

这些源码在 Windows 上准备，**尚未在 Swift Playgrounds 编译或在 iPad 上运行验证**。如果 Playgrounds 报源码或权限错误，请记录完整错误信息与行号，以便在仓库中修复。

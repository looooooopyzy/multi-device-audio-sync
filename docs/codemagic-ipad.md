# Codemagic → iPad Pro 真机验证

> LEGACY / EXPERIMENTAL — 本路线已暂停。当前主路线是 Windows Master + Safari 浏览器客户端；不需要 Apple Developer 付费账号或 Mac。

此仓库根目录已有 `codemagic.yaml`，其中：

- `ios-compile`：在 Codemagic 的 Mac 上编译 iOS App，不签名；用于先发现 Swift/Xcode 编译错误，产物不能装到 iPad。
- `ios-ipad-ad-hoc`：用 Ad Hoc 描述文件签名，产出可安装的 `.ipa`。构建依赖 Apple Developer Program 会员、签名证书、包含这台 iPad UDID 的 Ad Hoc 描述文件。若没有会员，仍可先运行 `ios-compile`。

## 1. 连接仓库并先编译

将仓库提交并推送到 Codemagic 可连接的 Git 托管平台。在 Codemagic 新增应用，选择该仓库，使用仓库根目录的 `codemagic.yaml`，先运行 **iOS compile check (unsigned)**。构建成功只说明原生 App 可以编译，不能视为真机测试通过。若失败，请保存首条 Swift/Xcode 错误和对应文件、行号。

## 2. 配置真机签名

1. 在 Apple Developer Portal 注册显式 App ID `org.multideviceaudio.node`。如果需要换 Bundle ID，同时修改 `ios-client/AudioNode.xcodeproj/project.pbxproj` 中两处 `PRODUCT_BUNDLE_IDENTIFIER`，以及 `codemagic.yaml` 的 `bundle_identifier`，三处必须一致。
2. 注册 iPad Pro 2021 的 UDID。Codemagic 团队账号可使用 **Team settings → iOS test devices** 的邀请流程；个人账号可在 Apple Developer Portal 手动注册。把 iPad 加入 Ad Hoc provisioning profile。
3. 在 Codemagic 的 **Team settings → codemagic.yaml settings → Code signing identities** 上传或生成 Apple Distribution 证书，并上传/获取相应的 Ad Hoc provisioning profile。两者必须匹配 App ID、开发团队和 iPad。
4. 运行 **iPad Ad Hoc IPA** 工作流。它先调用 `xcode-project use-profiles`，再调用 `xcode-project build-ipa`。成功后在构建页面的 Artifacts 中取得签名 `.ipa`，用 Codemagic 提供的设备安装链接/二维码在已注册 iPad 上安装。不要把未签名 `.app` 当作真机安装包。

Codemagic 的 [原生 iOS 构建指南](https://docs.codemagic.io/yaml-quick-start/building-a-native-ios-app/)、[签名指南](https://docs.codemagic.io/yaml-code-signing/signing-ios/)和[测试设备注册指南](https://docs.codemagic.io/testing/ios-provisioning/)说明了这些界面与凭据要求。Apple 的 [网络权限说明](https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy)说明 UDP 单播不需要 multicast entitlement，而广播需要。

## 3. 先测单播时钟同步

Windows 和 iPad 连接同一个非访客局域网。Windows 执行 `ipconfig`，记录正在使用的 Wi-Fi/以太网 IPv4；把网络配置为 **Private**，并按 [真机验证手册](device-verification.md)允许 Windows master 的入站 UDP。启动 `build/windows-master/windows-master.exe`。

若主机启动时报 `bind port 45671: 10048`，应先排查本机 45671 端口冲突；主机未成功监听时，iPad 无法完成同步。

在 iPad 打开 Audio Node，允许 **Local Network** 权限，保持 App 在前台。在顶部输入 Windows 的 IPv4，点 **Connect**；如果授权弹窗后的首次连接失败，再点一次。几秒后应看到：

- Windows 表格出现 iPad 节点，状态 `ONLINE`；
- iPad 状态 `CONNECTED`，`Samples` 持续增加，约每秒 4 个；
- 运行 2–3 分钟后，Windows 的 RTT、offset、jitter、drift 与 quality 有持续更新；
- 暂时关闭 iPad Wi-Fi，Windows 约 5 秒后显示 `TIMEOUT`，恢复后重新连接并确认采样恢复。

这条测试验证的是 **iPad 真机网络时钟同步**。当前 MVP 尚无音频播放，因此无法据此证明 1–5 ms 的实际出声对齐。

## 4. 再测自动发现

原生 App 监听 Windows 主机发出的 UDP 广播。iPadOS 真机收发广播需要 Apple 批准的 `com.apple.developer.networking.multicast` entitlement。拿到批准后，在 Apple App ID 中启用对应 capability，重新生成 Ad Hoc profile，并把 `ios-client/AudioNode/MulticastNetworking.entitlements` 配置为目标的 Code Signing Entitlements，重新构建和安装。之后不输入 IP，观察 App 是否自动找到主机。没有批准时，使用上述手动 IP 路径；不要把广播发现未通过当成时钟同步未通过。

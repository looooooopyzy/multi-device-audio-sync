# 浏览器扬声器客户端

启动 Windows Master 后，控制台只会打印可访问的局域网网址。在 Safari、Chrome 或 Edge 中打开该网址。Master 会直接提供本目录的网页，无需安装 npm 依赖或部署单独的服务器。点击 **启用扬声器**，等待页面显示同步质量和采样数，再按 **测试同步提示音**。手机和平板浏览器应保持在前台。

客户端使用 `performance.now()` 记录时钟同步的 T2/T3，接收 Master 计算的时钟映射模型，并通过 `AudioBufferSourceNode.start(when)` 提前排程 Click 和实时 PCM。每台设备的校准偏移量保存在 `localStorage`。`jitter-buffer.js` 按 Master 指定的未来播放时间缓冲每包 10 ms 的 Float32 PCM。

运行 Master 时使用 `--source tone` 可播放确定性的测试音；使用 `--source system` 可捕获 Windows 默认输出设备的混音。同步电脑音乐需将 VB-CABLE 设为默认输出，并把主控渲染设备选成独立的电脑扬声器，详见[实时音频说明](../docs/realtime-audio.md)。

同步音乐模式在 iPad 页面点击 **双向声音自动校准** 后，由 Windows 麦克风测量两端提示音，iPad 麦克风保持关闭和媒体播放路由。证书步骤见[双向声音自动校准](../docs/calibration.md)。播放中的网络波动会调整共同缓冲时间，麦克风测得的校准值单独保留。

如果网页已连接但没有声音，请检查 AudioContext 是否显示 `RUNNING`、设备音量是否打开、浏览器是否仍在前台。如果网页无法打开，请检查 Windows“专用网络”是否允许入站 TCP 17890，以及两台设备是否位于同一局域网且没有启用客户端隔离。
=4

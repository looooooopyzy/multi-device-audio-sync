# 双向声音自动校准

测试音模式下，Windows 与 iPad 都需要可用的麦克风和扬声器。同步音乐模式需先按 README 配好 VB-CABLE 或 Virtual Audio Driver，以及 Windows 实体输出设备；只需要 Windows 麦克风。把 Windows 麦克风放在通常听音的位置附近，保持环境安静。待时钟采样达到 8 次后，点击“双向声音自动校准”；校准过程约 6 秒，两端依次发出约 90 ms、方向不同的短扫频声。

测试音模式下，两个麦克风各自在同一段录音中找到两声；时间间隔减去预定的 1000 ms，可消去录入延迟。同步音乐模式下，iPad 麦克风保持关闭，Windows 麦克风检测两声之间的差值，避免 iPad 切换到通话音频路由。检测置信度需至少 0.20；失败时保留现有延迟。

自动结果会让发声较快的一端增加非负延迟。iPad 延迟保存在该浏览器中；Windows 延迟应用到选中的实体输出设备。同步音乐模式用 `--source system --local-stream-output --render-device N` 读取默认虚拟播放端点，再将 PCM 按时间戳送往实体电脑扬声器和 iPad。不能把渲染设备选成默认捕获设备，否则会回授；主程序会阻止相同设备编号。

## iPad Safari 的 HTTPS 准备

推荐直接双击构建目录中的 `windows-master/windows-calibrator.exe`（当前版本位于 `build/windows-master/`）。它会启动主控和 HTTPS 服务，显示 iPad 首次设置地址与后续使用的校准地址，并允许选择电脑输出设备。同步音乐前需安装 VB-CABLE 或 Virtual Audio Driver，把虚拟设备设为 Windows 默认播放设备，再在 GUI 选择真实扬声器。切换 Windows 默认输出时，GUI 会重新查找实体扬声器的设备编号并自动重连；状态显示“双端受控播放已就绪”后，电脑扬声器才会播放虚拟设备中的声音。iPad 第一次打开设置地址后，按网页顺序安装并信任证书。时钟同步后，点一次“双向声音自动校准”。

下列命令保留给需要手动指定 IP 或排查端口的开发者：

Safari 只能在安全上下文允许局域网页调用麦克风。Windows 上先运行主控，另开 PowerShell 运行 HTTPS 转发。此功能**不需要 Mac**。

```powershell
python -m pip install -r tools/requirements.txt
.\build\windows-master\windows-master.exe --source tone --local-stream-output
# 把示例 IP 换成主控实际打印的局域网 IP：
python tools/secure_web.py --ip 192.168.1.20
```

脚本只在本机生成证书，存放于忽略 Git 的 `.local-certs/`。将 `.local-certs/lan-root-ca.crt` 传到 iPad，例如通过局域网文件传输或邮件附件；只传 `.crt`，**不要传 `.key`**。在 iPad 打开证书并安装描述文件，随后进入“设置 → 通用 → 关于本机 → 证书信任设置”，为这个本地根证书启用完全信任。然后在 Safari 打开脚本打印的 `https://主控IP:17900/`。Windows 防火墙需允许 TCP 17900。安装的证书只用于信任这台主控的局域网网页；不再使用时可在 iPad 的描述文件和证书信任设置中移除。

证书绑定启动时传入的 IP。Windows 局域网 IP 改变后，使用新 IP 重新运行脚本，再访问新的 HTTPS 地址；根证书可继续使用。iPad 与 Windows 要在同一个可互访的局域网，不能由访客网络隔离。

## 播放中的网络动态调整

主控继续每 250 ms 更新时钟样本。浏览器每 2 秒报告迟到、断包和丢包计数；主控每 5 秒查看浏览器的过滤后 RTT、抖动及迟到情况。网络变差时，共同呈现延迟从 200 ms 提高到 300 或 500 ms；持续稳定约 30 秒后降低。每次调整会发布新的串流 ID，让各端以同一新时间轴接续播放。网络抖动不会覆盖声学校准值。

### 限制与验收

目前的扫频检测属于第一阶段实现，遇到很响的音乐、回声消除、蓝牙设备变化或 Windows 麦克风录不清校准声时可能失败。失败时会保留原延迟。同步音乐模式不请求 iPad 麦克风，完成后 iPad 音频模式保持“媒体播放”。实际声学误差和网络波动时的听感仍需通过 iPad 真机验证。

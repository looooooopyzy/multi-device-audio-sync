# UDP protocol v1

This UDP format is unchanged and remains active for legacy native and simulated clients. The primary browser transport uses the WebSocket protocol described below.

All integers are unsigned big-endian except T1/T2/T3, which are signed two's-complement 64-bit nanoseconds in big-endian order. Times are monotonic clock values, never UTC. Each datagram is exactly 96 bytes; extra, short, unknown-version, unknown-type, malformed name, and invalid platform packets are discarded. There is no native-struct memcpy or alignment dependency.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4d444131` (`MDA1`) |
| 4 | 2 | protocolVersion = 1 |
| 6 | 2 | messageType |
| 8 | 4 | sequence |
| 12 | 8 | deviceId (random persistent per app installation; simulator per process) |
| 20 | 8 | T1 |
| 28 | 8 | T2 |
| 36 | 8 | T3 |
| 44 | 1 | platform: 1 WINDOWS, 2 ANDROID, 3 IOS, 4 IPADOS, 5 SIMULATED |
| 45 | 1 | nameLength (0–48) |
| 46 | 2 | reserved = 0 |
| 48 | 48 | UTF-8 deviceName; bytes beyond nameLength are zero |

Message types: 1 `DISCOVERY_REQUEST`, 2 `DISCOVERY_RESPONSE`, 3 `SYNC_REQUEST`, 4 `SYNC_RESPONSE`, 5 `HEARTBEAT`. Values 16–22 are reserved for the future audio commands named in architecture.md. Discovery is broadcast to UDP 45670. The master sends sync requests from its UDP 45671 socket to the client's discovery response source endpoint. Clients send sync responses and heartbeats from that same endpoint back to master port 45671. Discovery responses may go to the requesting socket endpoint or master port 45671 (Network.framework UDP connections use the latter); the master listens on both.

For a sync exchange, the master stamps T1 immediately before send. The client stamps T2 upon socket receive and T3 just before response send. The master stamps T4 upon receive (T4 is local, never transmitted). The response echoes sequence and T1; deviceId identifies the responding node. The master accepts a response only if the pending request's sequence, T1, device ID and source IP/port match. Duplicates and late packets are ignored. A lost request simply expires, with no retransmission of its sequence. RTT = `(T4-T1)-(T3-T2)` and client-minus-master offset = `((T2-T1)+(T3-T4))/2`. No authentication is provided in this offline prototype; use a trusted LAN.

The master also sends one HEARTBEAT per second as UI telemetry: `sequence` = capped sample count, `T1` = estimated client-minus-master offset in ns at send time, `T2` = filtered RTT in ns, `T3` = drift in parts per billion (ppm × 1000). Client-to-master HEARTBEAT leaves these fields zero. This diagnostic use does not affect clock calculations.

## WebSocket control protocol v1

The master serves the page and WebSocket Upgrade at `http://WINDOWS_IP:17890/` and `ws://WINDOWS_IP:17890/ws`. Messages are small UTF-8 text frames with ASCII `|` separators. Names are printable ASCII without `|`, up to 48 bytes. Numeric fields are validated; malformed or oversized frames are ignored or closed. Master nanosecond values are decimal strings on the wire to avoid JSON integer precision loss. No browser UDP is used.

| Direction | Message | Meaning |
| --- | --- | --- |
| Client → master | `HELLO|deviceId|name` | Register a stable nonzero browser node ID. |
| Master → client | `WELCOME|masterName|deviceId` | Confirm registration. |
| Master → client | `SYNC_REQ|sequence|t1Ns` | T1 is QPC master time, stamped before sending. |
| Client → master | `SYNC_RESP|sequence|t1Ns|t2Ns|t3Ns` | T2 is message receive time and T3 send time from `performance.now()`. |
| Master → client | `SYNC_MODEL|masterRefMs|clientRefMs|rate|rttMs|filteredRttMs|jitterMs|quality|samples|rawOffsetMs` | Model from the existing shared estimator plus the latest unfiltered offset. |
| Client → master | `PING`, `AUDIO|0/1`, `CAL|ms` | Heartbeat, speaker state, and calibration telemetry. |
| Client → master | `CLICK` | Request a common click 1 second in the future. |
| Master → client | `CLICK_AT|masterTimeNs|durationMs|frequencyHz` | Schedule the click; currently 10 ms at 1 kHz. |

The master retains at most 32 simultaneous TCP clients, 16 KiB WebSocket payloads, and bounded output queues. It sends sync requests every 250 ms and model telemetry once per second. A browser node times out after 5 seconds without valid messages.

## Phase B PCM stream

When started with `--source tone` or `--source system`, the Master sends `STREAM_INFO|streamId|source|48000|2|200` after registration. Audio is sent as unmasked server-to-client **binary WebSocket frames**, only to registered nodes that report `AUDIO|1` and have at least eight clock samples. A slow client's audio frames are dropped when its output queue exceeds 64 KiB. The current source formats all audio to 48 kHz stereo Float32, with 480 frames per 10 ms packet. Each packet uses the same master clock domain as `CLICK_AT`.

The binary header is **48 bytes, big-endian**. The following 3,840 payload bytes are interleaved stereo IEEE 754 Float32 in **little-endian** order; total frame size is 3,888 bytes.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `0x4d444150` (`MDAP`) |
| 4 | 2 | version = 1 |
| 6 | 2 | header size = 48 |
| 8 | 4 | stream ID, nonzero |
| 12 | 4 | sequence |
| 16 | 8 | cumulative sample frame of packet's first sample |
| 24 | 8 | signed presentation master time, ns |
| 32 | 4 | sample rate = 48000 |
| 36 | 2 | channels = 2 |
| 38 | 2 | sample format = 1 (Float32) |
| 40 | 4 | frame count = 480 |
| 44 | 4 | flags; bit 0 signals capture discontinuity |

The browser validates exact frame size, format, stream ID, numeric samples, and sequence order. It uses `sampleFrame` for continuity and `presentationMasterTimeNs` for scheduling. A packet whose timestamp is too near or behind the local audio clock is skipped. The Phase A text protocol and legacy UDP protocol are unchanged.

## 双向声音校准和网络状态扩展

浏览器在用户开启扬声器和麦克风后发送 `CAL_REQUEST`。主控启动 Windows 麦克风后发送 `CAL_PROBES|session|windowsProbeMasterNs|ipadProbeMasterNs`，其中两次探测音相隔 1000 ms。Windows 播放上行扫频，iPad 播放下行扫频，以区分声源。iPad 在一段连续录音中检测两次探测音，返回 `CAL_RESULT|session|deltaMs|confidence|windowsConfidence|ipadConfidence`。`deltaMs` 是录到的两声间隔减去 1000 ms，后三项为 0–1 的匹配强度；原始录音不传输。

主控综合两边录音后发送 `CAL_APPLY|session|ipadDelayMs|windowsDelayMs|meanDeltaMs|windowsDeltaMs|ipadDeltaMs`。失败时发送 `CAL_REJECT|session|reason`；`session=0` 表示安排探测音前失败。旧值保持不变。浏览器每 2 秒发送 `NET|cumulativeLateCount`，主控结合 RTT 与抖动调整所有节点的共同呈现延迟。调整后发布新的 `STREAM_INFO` 和 stream ID；手动及声学校准值不随网络指标更改。

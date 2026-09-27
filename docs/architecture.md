# Architecture — Browser Speaker Phase B

The primary architecture is **Windows Master + Browser Speaker Nodes**. Windows owns the QPC master timeline, serves HTTP/WebSocket on TCP 17890, and keeps the original UDP discovery and synchronization on 45670/45671. Native Android/iOS clients remain legacy experiments; `shared/ClockSyncEngine` is unchanged and serves both transports.

```text
Music app → VB-CABLE default output → WASAPI loopback
                                       │
                             timestamped 48 kHz PCM
                                       │
                   sampleFrame + shared presentation time
                          ┌────────────┴────────────┐
                          ▼                         ▼
                 selected Windows output      WebSocket PCM
                                                    │
                                          browser jitter buffer
                                                    │
                                            iPad media output
```

The capture interface normalizes common Windows mix formats to 48 kHz stereo Float32. WASAPI packet QPC timestamps are in 100 ns units and are converted to the same master nanosecond domain as `monotonic_ns()`. A capture block has 480 frames. The master assigns a stream ID, sequence, cumulative sample frame, and a presentation time 200 ms after the first captured frame. Each browser gets the same packet timestamp. On the browser, `performance.now()` is mapped from master time through the existing NTP-style clock model, then anchored to `AudioContext.currentTime` for future `AudioBufferSourceNode.start(when)` calls.

The WebSocket server sends binary audio only after a node has registered, reported audio enabled, and collected eight clock samples. Per-client output queues are bounded; a slow client's audio packets are dropped. The browser maintains a bounded queue, schedules frames when they are within 120 ms of presentation, and counts sequence gaps or late frames. `CLICK_AT` remains an independent Phase A diagnostic.

## Windows output and loopback boundary

The local renderer can queue the same timestamped stream through a user selected WinMM output with initial leading silence. For system music, VB-CABLE provides a distinct capture endpoint while the selected physical Windows endpoint plays the captured PCM. The actual DAC latency remains unmeasured until acoustic calibration.

System loopback captures the **entire default multimedia render endpoint mix**. Replaying it to that same endpoint would feed the renderer back into loopback, so synchronized music requires a separate virtual capture endpoint and physical render endpoint. The GUI rejects a setup where the selected render device is also the default loopback endpoint. Without VB-CABLE, the program can still forward music to browsers, but Windows' direct sound is not synchronized. Process-specific capture that excludes Master remains future work.

The transport uses no authentication or encryption and is intended for a trusted LAN. This implementation does not use cloud services, music-service APIs, WebRTC, or Opus. VB-CABLE is an optional external Windows audio driver. Physical acoustic alignment and iOS Safari reliability require device verification.

# Browser Speaker Phase A Implementation Plan

**Goal:** Extend the existing Windows UDP clock master with LAN browser speaker nodes and a synchronized click test, while keeping the proven UDP/native paths intact.

**Architecture:** The existing `ClockSyncEngine` remains the sole estimator. A Windows WebSocket transport stamps browser clock exchanges and gives samples to that engine. An HTTP server serves a dependency-free web client; a local Windows click renderer and Web Audio clients schedule the same future master timestamp. Real-time capture, PCM streaming, and channel DSP remain Phase B+.

**Tech Stack:** C++17, Winsock, WinMM for the Phase A local test click, HTML/CSS/vanilla JavaScript, WebSocket, Web Audio, CMake, Python standard-library integration tests.

---

### Task 1: Protect the baseline

**Files:** `shared/**`, `windows-master/main.cpp`, `simulated-client/main.cpp`, `android-client/**`.

1. Inventory source and current documentation; record that `ClockSyncEngine` is transport-independent and UDP protocol v1 is fixed.
2. Run existing CTest and record Android APK build state.
3. Add only new shared interfaces needed for the web transport; preserve the UDP codec, estimator, and simulator behavior.

### Task 2: Add browser transport and LAN server

**Files:** Create `windows-master/web_server.h`, `windows-master/web_server.cpp`; modify `windows-master/CMakeLists.txt`, `windows-master/main.cpp`.

1. Add bounded HTTP static-file serving and same-port WebSocket upgrade on port 17890.
2. Parse bounded WebSocket text frames, validate protocol messages, and retain per-connection state.
3. Stamp `SYNC_REQ` immediately before send and `SYNC_RESP` on receive; feed `ClockSample` into existing `ClockSyncEngine`.
4. Publish model telemetry and show browser nodes in the Windows table.
5. Keep existing UDP loop active by default; add a web-only switch for isolated transport tests.

### Task 3: Add synchronized click output

**Files:** Create `windows-master/local_playback.h`, `windows-master/local_playback.cpp`; modify `windows-master/main.cpp`.

1. Schedule one 10 ms 1 kHz burst for a future master time (1 second lead).
2. Use the same target timestamp for Windows local output and `CLICK_AT` WebSocket messages.
3. Trigger from Windows CLI and web UI; report device-output latency as an acoustic calibration limitation.

### Task 4: Add universal web client

**Files:** Create `web-client/index.html`, `web-client/styles/main.css`, `web-client/src/{app,device-info,websocket-client,clock-sync,audio-engine,jitter-buffer,ui}.js`, `web-client/README.md`.

1. Connect to the page host's WebSocket, register a stable browser node, and exchange monotonic `performance.now()` timestamps.
2. Display RTT, offset, jitter, quality, node state, and audio state.
3. Unlock `AudioContext` through Enable Speaker; map future master timestamps to `AudioContext.currentTime` and schedule a generated burst.
4. Persist and apply per-node calibration offset. Include bounded scheduling fallback for late click messages.

### Task 5: Verify and document Phase A

**Files:** Add `tests/web_integration.py`; update `README.md`, `docs/{architecture,protocol,web-client,realtime-audio,calibration}.md`; add `android-client/LEGACY.md`, `ios-client/LEGACY.md`.

1. Build Windows targets; run existing CTest and a WebSocket/HTTP integration test exercising registration, repeated clock samples, telemetry, calibration, and click command.
2. Check browser JavaScript syntax and run a local browser smoke test where available.
3. Check Android build without changing its code.
4. Mark Codemagic and native iOS as paused; list Safari/iPad physical-device checks as outstanding.

**Completion gate:** Stop after Phase A. Phase B begins only on a later instruction.

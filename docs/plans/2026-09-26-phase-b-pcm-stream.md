# Phase B PCM Stream Implementation Plan

**Goal:** Stream 48 kHz stereo PCM from Windows to browser speakers against the existing master timeline, with system WASAPI loopback and a deterministic test source.

**Architecture:** The unchanged `ClockSyncEngine` maps browser time. A capture interface produces timestamped, normalized 10 ms blocks. The master assigns one `presentationMasterTimeNs` 200 ms after capture and sends versioned binary WebSocket packets. Browser jitter buffering and Web Audio schedule the same timeline. A local Windows stream renderer is available for the test source; system loopback local retransmission is disabled because it would feed back into the captured endpoint. Output-device latency needs physical calibration.

**Tech Stack:** C++17, WASAPI shared-mode loopback, WinMM, Winsock/WebSocket, vanilla JavaScript/Web Audio, Python standard library tests.

## Task 1: Packet contract

Create `shared/include/audio/audio_packet.h` and `shared/src/audio_packet.cpp`. Encode a fixed 48-byte, big-endian header carrying magic/version, stream ID, sequence, sample frame, master presentation time, 48000 Hz, 2 channels, Float32 format, 480 frames, flags. Payload is little-endian interleaved Float32. Add round-trip and rejection tests to `shared/tests/test_main.cpp`.

## Task 2: Capture source

Create `windows-master/audio_capture.h/.cpp` with `IAudioCaptureSource`, timestamped 480-frame blocks, and a bounded producer queue. Implement a deterministic tone source for transport testing. Implement `SystemLoopbackCapture` using the default render endpoint's WASAPI loopback stream, including common PCM/Float32 mix-format conversion and resampling to 48 kHz stereo. Expose errors in the CLI.

## Task 3: Master transport and playback

Add binary-frame broadcasting and stream metadata to `WebServer` while retaining all Phase A messages. In `main.cpp`, add an explicit `--source tone|system` option. Package capture blocks with one stream ID, sequence, sample frame, and +200 ms presentation time. Add a bounded local stream renderer. Disable local rendering for system loopback, because capturing and replaying the same default endpoint creates a feedback loop.

## Task 4: Browser playback

Extend the socket for ArrayBuffer frames. Decode and validate packets in `audio-packet.js`; queue by frame/timestamp in `jitter-buffer.js`; use `AudioBufferSourceNode.start(when)` for future PCM playback. Keep click test and per-device calibration. Show stream state, buffered audio, and dropped/late blocks.

## Task 5: Verification and documentation

Build Windows, run existing tests, add a Python binary WebSocket integration test for the tone stream, run JavaScript syntax checks and browser smoke testing. Update README, architecture, protocol, web-client, and real-time audio docs. Report system loopback/device and acoustic limitations honestly. Stop before Phase C process-specific capture.

# Real-time audio — Phase B

## Implemented

- `IAudioCaptureSource` produces timestamped 480-frame stereo blocks. The system loopback source opens the default Windows render endpoint in WASAPI shared loopback mode. It accepts common Float32 and PCM16/24/32 mix formats, selects the first two channels (mono is duplicated), and uses linear interpolation to normalize other sample rates to 48 kHz. The deterministic `tone` source makes transport tests repeatable.
- The Master sends one versioned 10 ms binary PCM packet per block. The packet records stream ID, sequence, cumulative sample frame, master presentation nanoseconds, 48 kHz, stereo, Float32, frame count, and discontinuity flag. The full layout is in [protocol.md](protocol.md).
- Presentation time is capture timestamp + a shared adaptive delay. Browser nodes queue packets and schedule ahead on Web Audio. `calibrationOffsetMs` shifts both click and PCM playback. The Windows renderer sends the same PCM to a separately selected output endpoint.
- PCM is uncompressed: 48,000 × 2 × 4 = 384,000 bytes/s, about 3.07 Mb/s per browser before transport overhead.

## Run

From the repository root, use `windows-master.exe --source tone` for a browser stream. Add `--local-stream-output` to hear the same tone on Windows. For synchronized system music, install VB-CABLE or Virtual Audio Driver, set its virtual speaker as the Windows default output, and start with `--source system --local-stream-output --render-device N`, where N is a different physical output endpoint. The GUI enumerates these outputs. Without a separate capture and render endpoint, the program only forwards music to browsers and does not synchronize the original Windows playback. Keep the page foregrounded and tap **Enable Speaker**. If UDP discovery ports are occupied, add `--web-only`.

## Limits and next work

System loopback is the full default output mix, not a `QQMusic.exe` selector. With a supported virtual playback endpoint, the captured stream is played through the selected Windows output and sent to browsers on one presentation timeline. The Windows microphone can measure the two acoustic probes while the iPad microphone stays off and its audio session stays in media playback. The physical speaker emission time still needs real-device measurement. Background mobile browsers may suspend timers or audio. A source that produces no audio may provide no WASAPI packets.

Future work can add `ProcessLoopbackCapture` behind the existing `IAudioCaptureSource` interface so one application can be captured without routing the whole Windows mix. A device-clock-aware renderer and channel assignment can follow after the two-speaker acoustic result is measured.

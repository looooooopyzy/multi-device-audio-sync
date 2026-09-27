import assert from "node:assert/strict";
import { JitterBuffer } from "../web-client/src/jitter-buffer.js";
import { AudioEngine } from "../web-client/src/audio-engine.js";
import { MicrophoneCapture } from "../web-client/src/microphone-capture.js";
import { setAudioSessionType } from "../web-client/src/audio-session.js";

const clock = { masterNsToPerformanceMs: (value) => Number(value) / 1e6 };
const buffer = new JitterBuffer();
const startMs = performance.now() + 200;
for (let index = 0; index < 10; index += 1) {
  assert.equal(buffer.push({ streamId: 1, sequence: index + 1,
    sampleFrame: index * 480, frameCount: 480, sampleRate: 48000,
    presentationNs: Math.round((startMs + index * 10) * 1e6),
    samples: new Float32Array(960).fill(index / 10), discontinuity: false }), true);
}
const first = buffer.takeReadyBatch(clock);
const second = buffer.takeReadyBatch(clock);
assert.equal(first.length, 5);
assert.equal(second.length, 5);

const starts = [];
const contexts = [];
const engine = new AudioEngine();
engine.context = {
  state: "running", currentTime: 1, sampleRate: 48000,
  destination: {},
  createBuffer(_channels, frames) {
    const channels = [new Float32Array(frames), new Float32Array(frames)];
    contexts.push(channels);
    return { getChannelData: (channel) => channels[channel] };
  },
  createBufferSource() {
    return { playbackRate: { value: 1 }, connect() {},
      start(when) { starts.push(when); } };
  }
};
assert.equal(engine.scheduleBatch(first, clock), true);
const firstEnd = engine.nextWhen;
assert.equal(engine.scheduleBatch(second, clock), true);
assert.equal(starts[1], firstEnd, "contiguous batches must have no gap or overlap");
assert.equal(contexts[0][0].length, 2400);
assert.equal(contexts[0][0][0], 0);
assert.ok(Math.abs(contexts[0][0][480] - 0.1) < 1e-6);
assert.equal(engine.scheduledBlocks, 10);

const early = new JitterBuffer();
early.push({ streamId: 2, sequence: 1, sampleFrame: 0,
  frameCount: 480, sampleRate: 48000,
  presentationNs: Math.round((performance.now() + 120) * 1e6),
  samples: new Float32Array(960), discontinuity: false });
assert.equal(early.takeReadyBatch(clock, 0), null);
const earlyBatch = early.takeReadyBatch(clock, -98);
assert.equal(earlyBatch?.length, 1,
  "negative calibration must advance the scheduling deadline");
engine.calibrationMs = -98;
assert.equal(engine.scheduleBatch(earlyBatch, clock), true,
  "a packet 120 ms in the future must still play at -98 ms calibration");
const session = { type: "auto" };
navigator.audioSession = session;
setAudioSessionType("play-and-record");
const microphone = new MicrophoneCapture({});
let stopped = false;
microphone.stream = { getTracks: () => [{ stop() { stopped = true; } }] };
microphone.close();
assert.equal(stopped, true);
assert.equal(session.type, "playback", "microphone close must restore media audio");
console.log("Browser PCM batching and gapless scheduling passed");

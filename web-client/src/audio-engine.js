import { calibrationProbe } from "./calibration-probe.js";
import { setAudioSessionType } from "./audio-session.js";

export class AudioEngine {
  constructor() {
    this.context = null;
    this.calibrationMs = 0;
    this.lateCount = 0;
    this.lastClick = "—";
    this.scheduledBlocks = 0;
    this.scheduledUntil = 0;
    this.streamId = 0;
    this.nextFrame = 0;
    this.nextWhen = 0;
  }
  get state() { return this.context ? this.context.state.toUpperCase() : "LOCKED"; }
  async enable() {
    const Constructor = window.AudioContext || window.webkitAudioContext;
    if (!Constructor) throw new Error("Web Audio is unavailable in this browser");
    setAudioSessionType("playback");
    if (!this.context) this.context = new Constructor();
    // Create and start the silent source inside the tap handler for iOS Safari.
    const silent = this.context.createBufferSource();
    silent.buffer = this.context.createBuffer(1, 1, this.context.sampleRate);
    silent.connect(this.context.destination);
    silent.start();
    await this.context.resume();
    setAudioSessionType("playback");
    if (this.context.state !== "running") throw new Error("AudioContext did not start");
  }
  scheduleClick(masterNs, clock, durationMs = 10, frequencyHz = 1000) {
    const context = this.context;
    const targetPerformanceMs = clock.masterNsToPerformanceMs(masterNs);
    if (!context || context.state !== "running" || targetPerformanceMs === null) {
      this.lastClick = "Not ready";
      return false;
    }
    const targetMs = targetPerformanceMs + this.calibrationMs;
    // Sample both clocks close together. AudioContext.currentTime is the
    // scheduling domain; performance.now() is the clock-sync domain.
    const p1 = performance.now();
    const audioNow = context.currentTime;
    const p2 = performance.now();
    const when = audioNow + (targetMs - (p1 + p2) / 2) / 1000;
    if (when < context.currentTime + 0.015) {
      this.lateCount += 1;
      this.lastClick = "Late; skipped";
      return false;
    }
    const frames = Math.max(1, Math.round(context.sampleRate * durationMs / 1000));
    const buffer = context.createBuffer(2, frames, context.sampleRate);
    for (let channel = 0; channel < 2; channel += 1) {
      const data = buffer.getChannelData(channel);
      for (let i = 0; i < frames; i += 1) {
        const envelope = Math.sin(Math.PI * i / frames);
        data[i] = 0.42 * envelope * Math.sin(2 * Math.PI * frequencyHz * i / context.sampleRate);
      }
    }
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    source.start(when);
    this.lastClick = "Scheduled " + Math.round(targetMs - p2) + " ms ahead";
    return true;
  }
  scheduleProbe(masterNs, clock) {
    const context = this.context;
    const targetMs = clock.masterNsToPerformanceMs(masterNs);
    if (!context || context.state !== "running" || targetMs === null) return false;
    const p1 = performance.now();
    const audioNow = context.currentTime;
    const p2 = performance.now();
    const when = audioNow + (targetMs + this.calibrationMs - (p1 + p2) / 2) / 1000;
    if (when < context.currentTime + 0.05) return false;
    const samples = calibrationProbe(context.sampleRate, "ipad");
    const buffer = context.createBuffer(2, samples.length, context.sampleRate);
    buffer.getChannelData(0).set(samples);
    buffer.getChannelData(1).set(samples);
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    source.start(when);
    return true;
  }
  scheduleBatch(packets, clock) {
    const context = this.context;
    if (!context || context.state !== "running" || !packets.length) return false;
    const packet = packets[0];
    const targetMs = clock.masterNsToPerformanceMs(String(packet.presentationNs));
    if (targetMs === null) return false;
    const p1 = performance.now();
    const audioNow = context.currentTime;
    const p2 = performance.now();
    const desired = audioNow + (targetMs + this.calibrationMs - (p1 + p2) / 2) / 1000;
    let when = desired;
    let playbackRate = 1;
    if (this.streamId === packet.streamId && this.nextFrame === packet.sampleFrame &&
        Math.abs(desired - this.nextWhen) < 0.02) {
      // Consecutive PCM must touch exactly: changing start time by even 50 us
      // can insert several zero samples and click at every batch boundary.
      when = this.nextWhen;
      const duration = packets.reduce((seconds, item) => seconds +
        item.frameCount / item.sampleRate, 0);
      playbackRate = Math.max(0.999, Math.min(1.001,
        1 - (desired - when) * 0.2 / duration));
    }
    if (when < context.currentTime + 0.015) {
      this.lateCount += 1;
      this.nextFrame = 0;
      return false;
    }
    const frames = packets.reduce((count, item) => count + item.frameCount, 0);
    const buffer = context.createBuffer(2, frames, packet.sampleRate);
    for (let channel = 0; channel < 2; channel += 1) {
      const data = buffer.getChannelData(channel);
      let at = 0;
      for (const item of packets) {
        for (let i = 0; i < item.frameCount; i += 1)
          data[at + i] = item.samples[2 * i + channel];
        at += item.frameCount;
      }
    }
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.playbackRate.value = playbackRate;
    source.connect(context.destination);
    source.start(when);
    this.streamId = packet.streamId;
    this.nextFrame = packet.sampleFrame + frames;
    this.nextWhen = when + frames / packet.sampleRate / playbackRate;
    this.scheduledUntil = Math.max(this.scheduledUntil, this.nextWhen);
    this.scheduledBlocks += packets.length;
    return true;
  }
  get scheduledAheadMs() {
    return this.context ? Math.max(0, (this.scheduledUntil - this.context.currentTime) * 1000) : 0;
  }
}

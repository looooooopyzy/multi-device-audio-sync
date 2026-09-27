export class JitterBuffer {
  constructor(targetMs = 200) {
    this.targetMs = targetMs;
    this.queue = [];
    this.underruns = 0;
    this.dropped = 0;
    this.streamId = 0;
    this.lastSequence = null;
  }
  setTarget(ms) {
    if (!Number.isInteger(ms) || ms < 100 || ms > 1000)
      throw new RangeError("buffer target");
    this.targetMs = ms;
  }
  push(packet) {
    if (!Number.isFinite(packet.sampleFrame) || packet.frameCount !== 480 ||
        packet.sampleRate !== 48000) return false;
    if (this.streamId !== packet.streamId || packet.discontinuity) {
      this.queue = [];
      this.lastSequence = null;
      this.streamId = packet.streamId;
    }
    if (this.lastSequence !== null && packet.sequence > this.lastSequence + 1)
      this.underruns += packet.sequence - this.lastSequence - 1;
    if (this.lastSequence !== null && packet.sequence <= this.lastSequence) {
      this.dropped += 1;
      return false;
    }
    this.lastSequence = packet.sequence;
    this.queue.push(packet);
    if (this.queue.length > 60) { this.queue.shift(); this.dropped += 1; }
    return true;
  }
  takeReadyBatch(clock, calibrationMs = 0, maxPackets = 5) {
    if (!this.queue.length) return null;
    const target = clock.masterNsToPerformanceMs(String(this.queue[0].presentationNs));
    if (target === null) return null;
    const aheadMs = target + calibrationMs - performance.now();
    // Collect a few packets while there is time, but never hold a partial
    // batch close to its playback deadline.
    if (this.queue.length < maxPackets && aheadMs > 60) return null;
    const batch = [this.queue.shift()];
    while (batch.length < maxPackets && this.queue.length) {
      const previous = batch[batch.length - 1];
      const next = this.queue[0];
      if (next.streamId !== previous.streamId ||
          next.sampleFrame !== previous.sampleFrame + previous.frameCount ||
          Math.abs((next.presentationNs - previous.presentationNs) / 1e6 - 10) > 2)
        break;
      batch.push(this.queue.shift());
    }
    return batch;
  }
  reset() { this.queue = []; this.lastSequence = null; this.streamId = 0; }
  get bufferedMs() {
    return this.queue.reduce((ms, packet) => ms + 1000 * packet.frameCount / packet.sampleRate, 0);
  }
}

class CalibrationRecorderProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.recording = null;
    this.position = 0;
    this.port.onmessage = ({ data }) => {
      if (data?.type === "START" && Number.isInteger(data.frames) &&
          data.frames > 0 && data.frames <= sampleRate * 12) {
        this.recording = new Float32Array(data.frames);
        this.position = 0;
        this.port.postMessage({ type: "READY" });
      } else if (data?.type === "STOP") {
        this.recording = null;
      }
    };
  }
  process(inputs) {
    const channel = inputs[0]?.[0];
    if (this.recording && channel?.length) {
      const count = Math.min(channel.length, this.recording.length - this.position);
      this.recording.set(channel.subarray(0, count), this.position);
      this.position += count;
      if (this.position >= this.recording.length) {
        const recording = this.recording;
        this.recording = null;
        this.port.postMessage({ type: "DONE", sampleRate, samples: recording.buffer },
                              [recording.buffer]);
      }
    }
    return true;
  }
}
registerProcessor("calibration-recorder", CalibrationRecorderProcessor);

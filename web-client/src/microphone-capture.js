import { setAudioSessionType } from "./audio-session.js";

export class MicrophoneCapture {
  constructor(context) {
    this.context = context;
    this.stream = null;
    this.source = null;
    this.node = null;
    this.silent = null;
    this.timer = null;
    this.rejectRecording = null;
  }
  async open() {
    if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia ||
        !this.context?.audioWorklet) throw new Error("请通过受信任的 HTTPS 页面打开校准功能");
    setAudioSessionType("play-and-record");
    try {
      this.stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false }
      });
    } catch (error) {
      setAudioSessionType("playback");
      throw error;
    }
    try {
      await this.context.audioWorklet.addModule("/src/calibration-recorder-worklet.js");
      this.node = new AudioWorkletNode(this.context, "calibration-recorder");
      this.source = this.context.createMediaStreamSource(this.stream);
      this.silent = this.context.createGain();
      this.silent.gain.value = 0;
      this.source.connect(this.node).connect(this.silent).connect(this.context.destination);
    } catch (error) {
      this.close();
      throw error;
    }
  }
  record(durationMs) {
    if (!this.node || !Number.isFinite(durationMs) || durationMs <= 0 || durationMs > 12000)
      return Promise.reject(new Error("麦克风未就绪"));
    return new Promise((resolve, reject) => {
      this.rejectRecording = reject;
      this.timer = setTimeout(() => {
        this.timer = null;
        this.rejectRecording = null;
        reject(new Error("麦克风录音超时"));
      }, durationMs + 2500);
      this.node.port.onmessage = ({ data }) => {
        if (data?.type !== "DONE") return;
        clearTimeout(this.timer);
        this.timer = null;
        this.rejectRecording = null;
        resolve({ samples: new Float32Array(data.samples), sampleRate: data.sampleRate });
      };
      this.node.port.postMessage({ type: "START", frames: Math.round(this.context.sampleRate * durationMs / 1000) });
    });
  }
  close() {
    clearTimeout(this.timer);
    this.timer = null;
    if (this.rejectRecording) this.rejectRecording(new Error("录音已停止"));
    this.rejectRecording = null;
    this.node?.port.postMessage({ type: "STOP" });
    this.source?.disconnect();
    this.node?.disconnect();
    this.silent?.disconnect();
    this.stream?.getTracks().forEach((track) => track.stop());
    setAudioSessionType("playback");
    this.source = null; this.node = null; this.silent = null; this.stream = null;
  }
}

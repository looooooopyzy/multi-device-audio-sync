import { detectProbePair, PROBE_GAP_MS } from "./calibration-probe.js";

self.onmessage = ({ data }) => {
  const samples = new Float32Array(data.samples);
  const result = detectProbePair(samples, data.sampleRate, PROBE_GAP_MS);
  self.postMessage(result);
};

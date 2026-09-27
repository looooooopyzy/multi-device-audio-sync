export const PROBE_DURATION_MS = 90;
export const PROBE_GAP_MS = 1000;
export const MIN_CONFIDENCE = 0.20;
const ANALYSIS_RATE = 12000;

export function calibrationProbe(sampleRate, kind = "windows") {
  if (!Number.isFinite(sampleRate) || sampleRate < 8000 || sampleRate > 192000) return new Float32Array();
  const count = Math.round(sampleRate * PROBE_DURATION_MS / 1000);
  const output = new Float32Array(count);
  const duration = PROBE_DURATION_MS / 1000;
  for (let i = 0; i < count; i += 1) {
    const time = i / sampleRate;
    const rise = Math.sin(Math.PI * time / duration);
    const start = kind === "ipad" ? 3900 : 900;
    const end = kind === "ipad" ? 900 : 3900;
    const phase = 2 * Math.PI * (start * time + (end - start) * time * time / (2 * duration));
    output[i] = 0.45 * rise * rise * Math.sin(phase);
  }
  return output;
}

function atAnalysisRate(input, sampleRate) {
  const result = new Float32Array(Math.floor(input.length * ANALYSIS_RATE / sampleRate));
  for (let i = 0; i < result.length; i += 1) {
    const position = i * sampleRate / ANALYSIS_RATE;
    const left = Math.floor(position);
    const right = Math.min(left + 1, input.length - 1);
    result[i] = input[left] + (input[right] - input[left]) * (position - left);
  }
  return result;
}

function highPass(samples) {
  const rc = 1 / (2 * Math.PI * 700);
  const alpha = rc / (rc + 1 / ANALYSIS_RATE);
  for (let stage = 0; stage < 2; stage += 1) {
    let previousInput = 0, previousOutput = 0;
    for (let i = 0; i < samples.length; i += 1) {
      const input = samples[i];
      const output = alpha * (previousOutput + input - previousInput);
      samples[i] = output;
      previousInput = input;
      previousOutput = output;
    }
  }
}

export function detectProbePair(recording, sampleRate, gapMs = PROBE_GAP_MS) {
  const invalid = { valid: false, deltaMs: 0, confidence: 0,
                    windowsConfidence: 0, ipadConfidence: 0 };
  if (!(recording instanceof Float32Array) || !Number.isFinite(sampleRate) ||
      sampleRate < 8000 || sampleRate > 192000 || recording.length === 0 ||
      recording.length > sampleRate * 12 || !Number.isFinite(gapMs) ||
      gapMs < 300 || gapMs > 3000) return invalid;
  const samples = atAnalysisRate(recording, sampleRate);
  const windowsReference = calibrationProbe(ANALYSIS_RATE, "windows");
  const ipadReference = calibrationProbe(ANALYSIS_RATE, "ipad");
  if (samples.length < windowsReference.length + Math.max(0, gapMs - 500) * ANALYSIS_RATE / 1000)
    return invalid;
  highPass(samples);
  highPass(windowsReference);
  highPass(ipadReference);
  let windowsEnergy = 0, ipadEnergy = 0;
  for (const value of windowsReference) windowsEnergy += value * value;
  for (const value of ipadReference) ipadEnergy += value * value;
  const energy = new Float64Array(samples.length + 1);
  for (let i = 0; i < samples.length; i += 1)
    energy[i + 1] = energy[i] + samples[i] * samples[i];
  const windowsCandidates = [], ipadCandidates = [];
  let windowsConfidence = 0, ipadConfidence = 0;
  for (let frame = 0; frame + windowsReference.length <= samples.length; frame += 2) {
    const windowEnergy = energy[frame + windowsReference.length] - energy[frame];
    if (windowEnergy < 1e-8) continue;
    let windowsDot = 0, ipadDot = 0;
    for (let j = 0; j < windowsReference.length; j += 1) {
      windowsDot += samples[frame + j] * windowsReference[j];
      ipadDot += samples[frame + j] * ipadReference[j];
    }
    const windowsScore = Math.min(1, Math.abs(windowsDot) /
      Math.sqrt(windowsEnergy * windowEnergy));
    const ipadScore = Math.min(1, Math.abs(ipadDot) /
      Math.sqrt(ipadEnergy * windowEnergy));
    windowsConfidence = Math.max(windowsConfidence, windowsScore);
    ipadConfidence = Math.max(ipadConfidence, ipadScore);
    if (windowsScore >= 0.16) windowsCandidates.push({ frame, score: windowsScore });
    if (ipadScore >= 0.16) ipadCandidates.push({ frame, score: ipadScore });
  }
  const selectPeaks = (candidates) => {
    candidates.sort((a, b) => b.score - a.score);
    const peaks = [];
    for (const candidate of candidates) {
      if (!peaks.some((peak) => Math.abs(peak.frame - candidate.frame) < ANALYSIS_RATE / 20))
        peaks.push(candidate);
      if (peaks.length >= 24) break;
    }
    return peaks;
  };
  const windowsPeaks = selectPeaks(windowsCandidates);
  const ipadPeaks = selectPeaks(ipadCandidates);
  let best = -Infinity;
  let result = { ...invalid, windowsConfidence, ipadConfidence };
  for (const first of windowsPeaks) for (const second of ipadPeaks) {
    if (second.frame <= first.frame) continue;
    const actualGapMs = (second.frame - first.frame) * 1000 / ANALYSIS_RATE;
    const deltaMs = actualGapMs - gapMs;
    if (Math.abs(deltaMs) > 500) continue;
    const confidence = Math.min(first.score, second.score);
    const quality = confidence - 0.02 * Math.abs(deltaMs) / 500;
    if (quality > best) {
      best = quality;
      result = { valid: confidence >= MIN_CONFIDENCE, deltaMs, confidence,
                 windowsConfidence, ipadConfidence,
                 firstMs: first.frame * 1000 / ANALYSIS_RATE,
                 secondMs: second.frame * 1000 / ANALYSIS_RATE };
    }
  }
  return result;
}

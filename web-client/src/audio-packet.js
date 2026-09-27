const MAGIC = 0x4d444150;
const HEADER = 48;
const FRAMES = 480;
const CHANNELS = 2;

function uint64(view, offset) {
  const value = view.getUint32(offset, false) * 4294967296 + view.getUint32(offset + 4, false);
  return Number.isSafeInteger(value) ? value : null;
}

export function decodeAudioPacket(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength !== HEADER + FRAMES * CHANNELS * 4) return null;
  const view = new DataView(buffer);
  if (view.getUint32(0, false) !== MAGIC || view.getUint16(4, false) !== 1 ||
      view.getUint16(6, false) !== HEADER || view.getUint32(32, false) !== 48000 ||
      view.getUint16(36, false) !== CHANNELS || view.getUint16(38, false) !== 1 ||
      view.getUint32(40, false) !== FRAMES) return null;
  const streamId = view.getUint32(8, false);
  const sampleFrame = uint64(view, 16);
  const presentationNs = uint64(view, 24);
  if (!streamId || sampleFrame === null || presentationNs === null) return null;
  const samples = new Float32Array(FRAMES * CHANNELS);
  for (let i = 0; i < samples.length; i += 1) {
    const value = view.getFloat32(HEADER + i * 4, true);
    if (!Number.isFinite(value)) return null;
    samples[i] = value;
  }
  return { streamId, sequence: view.getUint32(12, false), sampleFrame,
           presentationNs, sampleRate: 48000, frameCount: FRAMES, samples,
           discontinuity: Boolean(view.getUint32(44, false) & 1) };
}

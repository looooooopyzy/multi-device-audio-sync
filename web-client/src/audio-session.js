export function setAudioSessionType(type) {
  try {
    if (navigator.audioSession) navigator.audioSession.type = type;
  } catch (_) {
    // Older browsers use their own audio routing policy.
  }
}

export function audioSessionType() {
  try { return navigator.audioSession?.type || "unsupported"; }
  catch (_) { return "unsupported"; }
}

function saved(key) {
  try { return localStorage.getItem(key); } catch (_) { return null; }
}
function persist(key, value) {
  try { localStorage.setItem(key, value); } catch (_) { /* private browsing fallback */ }
}
export function deviceIdentity() {
  let id = saved("mda-device-id");
  if (!id || !/^[1-9][0-9]{0,15}$/.test(id)) {
    const words = new Uint32Array(2);
    if (globalThis.crypto && crypto.getRandomValues) crypto.getRandomValues(words);
    else { words[0] = Math.random() * 0xffffffff; words[1] = Math.random() * 0xffffffff; }
    id = String(1 + ((words[0] * 65536 + (words[1] & 65535)) % 9007199254740990));
    persist("mda-device-id", id);
  }
  const ua = navigator.userAgent || "";
  let name = "Browser Speaker";
  if (/iPad/i.test(ua) || (/Macintosh/i.test(ua) && navigator.maxTouchPoints > 1)) name = "iPad Safari";
  else if (/iPhone/i.test(ua)) name = "iPhone Safari";
  else if (/Android/i.test(ua)) name = "Android Chrome";
  else if (/Windows/i.test(ua)) name = "Windows Browser";
  else if (/Linux/i.test(ua)) name = "Linux Browser";
  else if (/Macintosh/i.test(ua)) name = "Mac Browser";
  return { id, name };
}
export function loadCalibration() {
  const value = Number(saved("mda-calibration-ms") || 0);
  return Number.isFinite(value) && value >= -500 && value <= 500 ? value : 0;
}
export function saveCalibration(value) { persist("mda-calibration-ms", String(value)); }

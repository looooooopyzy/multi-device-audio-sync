import { audioSessionType } from "./audio-session.js";

const element = (id) => document.getElementById(id);
const set = (id, text) => { element(id).textContent = text; };
const connectionLabels = {
  CONNECTING: "正在连接",
  CONNECTED: "已连接",
  DISCONNECTED: "已断开",
  "NETWORK ERROR": "网络错误"
};
const audioLabels = {
  LOCKED: "未启用",
  RUNNING: "运行中",
  SUSPENDED: "已暂停",
  INTERRUPTED: "已中断",
  CLOSED: "已关闭"
};
const streamLabels = { OFF: "关闭", TONE: "测试音", SYSTEM: "系统音频",
  "SYSTEM-ROUTED": "系统音乐（双端同步）" };
const qualityLabels = {
  EXCELLENT: "优秀",
  GOOD: "良好",
  FAIR: "一般",
  POOR: "较差",
  UNSTABLE: "不稳定"
};
const deviceLabels = {
  "Browser Speaker": "浏览器扬声器",
  "Windows Browser": "Windows 浏览器",
  "Linux Browser": "Linux 浏览器",
  "Mac Browser": "Mac 浏览器"
};
const metric = (number, digits = 2) => Number.isFinite(number)
  ? number.toLocaleString("zh-CN", { minimumFractionDigits: digits, maximumFractionDigits: digits }) + " ms" : "—";
function clickLabel(value) {
  if (value === "Not ready") return "尚未就绪";
  if (value === "Late; skipped") return "播放时间已过，已跳过";
  const scheduled = /^Scheduled (-?\d+) ms ahead$/.exec(value);
  if (scheduled) return `已排程，约 ${scheduled[1]} ms 后播放`;
  return value;
}
export function render(state) {
  set("connection-status", connectionLabels[state.connection] || state.connection);
  set("master-name", state.master === "Windows-Master" ? "Windows 主控" : state.master);
  set("device-name", deviceLabels[state.device] || state.device);
  const model = state.clock.model;
  set("rtt", model ? metric(model.rttMs) : "—");
  set("filtered-rtt", model ? metric(model.filteredRttMs) : "—");
  set("offset", model ? (model.rawOffsetMs >= 0 ? "+" : "") + metric(model.rawOffsetMs) : "—");
  set("filtered-offset", model ? (model.offsetMs >= 0 ? "+" : "") + metric(model.offsetMs) : "—");
  set("jitter", model ? metric(model.jitterMs) : "—");
  set("drift", model ? (model.driftPpm >= 0 ? "+" : "") + model.driftPpm.toFixed(2) + " ppm" : "—");
  set("quality", model ? (qualityLabels[model.quality] || model.quality) + " · " + model.samples + " 次采样" : "—");
  set("audio-state", audioLabels[state.audio.state] || state.audio.state);
  const session = audioSessionType();
  set("audio-session", { playback: "媒体播放", "play-and-record": "通话录音",
    auto: "浏览器自动", unsupported: "浏览器未提供" }[session] || session);
  set("stream-source", streamLabels[state.stream.source] || state.stream.source);
  set("stream-packets", String(state.stream.received));
  set("buffer-target", state.buffer.targetMs + " ms");
  set("buffered-audio", Math.round(state.buffer.bufferedMs + state.audio.scheduledAheadMs) + " ms");
  set("underruns", String(state.audio.lateCount + state.buffer.underruns + state.buffer.dropped));
  set("last-click", clickLabel(state.audio.lastClick));
  set("notice", state.notice);
  set("calibration-status", state.calibrationStatus);
  element("auto-calibrate").disabled = state.calibrating || state.connection !== "CONNECTED" ||
    state.stream.source === "SYSTEM" ||
    !model || model.samples < 8;
  element("auto-calibrate").textContent = state.calibrating ? "正在校准…" : "双向声音自动校准";
  element("test-click").disabled = state.connection !== "CONNECTED" ||
    state.audio.state !== "RUNNING" || !model || model.samples < 8;
  element("enable-speaker").disabled = state.calibrating;
  element("enable-speaker").textContent = state.audio.state === "RUNNING" ? "扬声器已启用" : "启用扬声器";
}

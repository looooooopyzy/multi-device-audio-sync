import { deviceIdentity, loadCalibration, saveCalibration } from "./device-info.js";
import { SpeakerSocket } from "./websocket-client.js";
import { BrowserClockSync } from "./clock-sync.js";
import { AudioEngine } from "./audio-engine.js";
import { decodeAudioPacket } from "./audio-packet.js";
import { JitterBuffer } from "./jitter-buffer.js";
import { render } from "./ui.js";
import { MicrophoneCapture } from "./microphone-capture.js";
import { detectProbePair, PROBE_GAP_MS } from "./calibration-probe.js";

function analyzeProbePair(samples, sampleRate) {
  if (typeof Worker !== "function")
    return Promise.resolve(detectProbePair(samples, sampleRate, PROBE_GAP_MS));
  return new Promise((resolve, reject) => {
    let worker;
    try { worker = new Worker("/src/calibration-worker.js", { type: "module" }); }
    catch (_) { resolve(detectProbePair(samples, sampleRate, PROBE_GAP_MS)); return; }
    const timeout = setTimeout(() => {
      worker.terminate(); reject(new Error("iPad 校准声分析超时"));
    }, 10000);
    worker.onmessage = ({ data }) => {
      clearTimeout(timeout); worker.terminate(); resolve(data);
    };
    worker.onerror = () => {
      clearTimeout(timeout); worker.terminate(); reject(new Error("iPad 校准声分析失败"));
    };
    worker.postMessage({ samples: samples.buffer, sampleRate }, [samples.buffer]);
  });
}

const identity = deviceIdentity();
const clock = new BrowserClockSync();
const audio = new AudioEngine();
const buffer = new JitterBuffer(200);
const state = { device: identity.name, master: "—", connection: "CONNECTING",
                clock, audio, buffer, stream: { source: "OFF", id: 0, received: 0 },
                notice: "正在连接 Windows 主控…", calibrating: false,
                calibrationStatus: "需要本设备与 Windows 麦克风都能听到两次校准声。" };
let microphone = null;
let calibrationSession = 0;
function finishCalibration(message) {
  microphone?.close(); microphone = null;
  state.calibrating = false;
  state.calibrationStatus = message;
  update();
}
const calibrationInput = document.getElementById("calibration");
audio.calibrationMs = loadCalibration();
calibrationInput.value = String(audio.calibrationMs);
const update = () => render(state);
function musicTimingStatus() {
  const afterCapture = Math.round(buffer.targetMs + audio.calibrationMs);
  if (afterCapture < 60)
    return "正在等待主控增加发送提前量；当前校准会让音频包来不及播放。";
  return `音乐排程在 Windows 捕获后约 ${afterCapture} ms；` +
    "Windows 与 iPad 都由主控按时间戳播放，可通过自动校准调整声学延迟。";
}
const socket = new SpeakerSocket(identity, (message, receivedMs) => {
  if (message instanceof ArrayBuffer) {
    const packet = decodeAudioPacket(message);
    if (packet && packet.streamId === state.stream.id && buffer.push(packet))
      state.stream.received += 1;
    return;
  }
  const fields = String(message).split("|");
  if (fields[0] === "WELCOME" && fields.length === 3) {
    state.master = fields[1];
    state.notice = "正在收集时钟样本，请保持页面打开。";
    socket.send("CAL|" + audio.calibrationMs);
    socket.send("AUDIO|" + (audio.state === "RUNNING" ? "1" : "0"));
  } else if (fields[0] === "SYNC_REQ") {
    clock.handleRequest(fields, receivedMs, (text) => socket.send(text));
  } else if (fields[0] === "SYNC_MODEL") {
    if (clock.updateModel(fields)) state.notice = "时钟已同步，可以测试同步提示音。";
  } else if (fields[0] === "STREAM_INFO" && fields.length === 6) {
    const id = Number(fields[1]);
    if (Number.isSafeInteger(id) && id > 0 && fields[3] === "48000" && fields[4] === "2") {
      if (state.stream.id !== id) buffer.reset();
      state.stream.id = id;
      state.stream.source = fields[2].toUpperCase();
      buffer.setTarget(Number(fields[5]));
      if(state.stream.source === "SYSTEM-ROUTED") {
        state.notice = "电脑音乐捕获已就绪；在 Windows 播放歌曲，并保持 iPad 页面在前台。";
        state.calibrationStatus = musicTimingStatus();
      } else if (state.stream.source === "SYSTEM") {
        state.notice = "正在转发 Windows 系统音乐；尚未配置电脑扬声器同步输出。";
        state.calibrationStatus = "安装并启用 VB-CABLE 后，主控才能控制电脑扬声器并完成声学校准。";
      } else {
        state.notice = "音频流已就绪，点击“启用扬声器”即可播放。";
        state.calibrationStatus = "需要本设备与 Windows 麦克风都能听到两次校准声。";
      }
    }
  } else if (fields[0] === "CLICK_AT" && fields.length === 4) {
    audio.scheduleClick(fields[1], clock, Number(fields[2]), Number(fields[3]));
  } else if (fields[0] === "CAL_PROBES" && fields.length === 4 && state.calibrating &&
             (microphone || state.stream.source === "SYSTEM-ROUTED")) {
    const session = Number(fields[1]);
    if (!Number.isSafeInteger(session) || session <= 0 || !/^[0-9]+$/.test(fields[2]) ||
        !/^[0-9]+$/.test(fields[3])) return;
    calibrationSession = session;
    const recording = microphone?.record(5000);
    if (!audio.scheduleProbe(fields[3], clock)) {
      finishCalibration("校准声排程太晚，请保持页面在前台后重试。");
      socket.send("CAL_RESULT|" + session + "|0|0|0|0");
      return;
    }
    if (!microphone) {
      state.calibrationStatus = "本设备校准声已排程；Windows 麦克风正在测量两端的实际播放差值。";
      update();
      return;
    }
    state.calibrationStatus = "正在录音：Windows 与本设备将各播放一次短校准声。";
    recording.then(async ({ samples, sampleRate }) => {
      const result = await analyzeProbePair(samples, sampleRate);
      if (!state.calibrating || session !== calibrationSession) return;
      socket.send("CAL_RESULT|" + session + "|" + result.deltaMs + "|" + result.confidence +
        "|" + result.windowsConfidence + "|" + result.ipadConfidence);
      state.calibrationStatus = result.valid ? "本设备录音完成，等待 Windows 麦克风结果。" :
        `本设备录音：Windows 声 ${Math.round(result.windowsConfidence * 100)}%，` +
        `本设备声 ${Math.round(result.ipadConfidence * 100)}%；等待主控检查。`;
      microphone?.close(); microphone = null;
      update();
    }).catch((error) => {
      if (state.calibrating && session === calibrationSession) {
        socket.send("CAL_RESULT|" + session + "|0|0|0|0");
        finishCalibration(error.message);
      }
    });
  } else if (fields[0] === "CAL_APPLY" && fields.length === 7) {
    if (Number(fields[1]) !== calibrationSession) return;
    const deviceDelay = Number(fields[2]);
    setCalibration(deviceDelay);
    finishCalibration(state.stream.source === "SYSTEM-ROUTED" ?
      `校准完成：本设备延迟 ${fields[2]} ms，Windows 延迟 ${fields[3]} ms；` +
      `Windows 麦克风测得差值 ${fields[5]} ms。本设备保持媒体播放模式。` :
      `校准完成：本设备延迟 ${fields[2]} ms，Windows 延迟 ${fields[3]} ms；` +
      `Windows 麦克风偏差 ${fields[5]} ms，本设备麦克风偏差 ${fields[6]} ms，平均 ${fields[4]} ms。`);
  } else if (fields[0] === "CAL_REJECT" && fields.length === 3) {
    if (Number(fields[1]) !== calibrationSession && calibrationSession !== 0) return;
    finishCalibration("自动校准未应用：" + fields[2]);
  }
  update();
}, (connection) => {
  state.connection = connection;
  if (connection !== "CONNECTED") {
    if (state.calibrating) finishCalibration("连接中断，校准已取消。");
    clock.reset();
    buffer.reset();
    state.master = "—";
    state.notice = "正在等待 Windows 主控连接。";
  }
  update();
});

document.getElementById("auto-calibrate").addEventListener("click", async () => {
  if (state.calibrating) return;
  state.calibrating = true;
    const windowsOnly = state.stream.source === "SYSTEM-ROUTED";
  state.calibrationStatus = windowsOnly ? "准备使用 Windows 麦克风校准…" :
    "正在申请本设备麦克风权限…";
  update();
  try {
    await audio.enable();
    calibrationSession = 0;
    if (!windowsOnly) {
      microphone = new MicrophoneCapture(audio.context);
      await microphone.open();
    }
    socket.send("AUDIO|1");
    socket.send("CAL_REQUEST");
    state.calibrationStatus = windowsOnly ?
      "本设备麦克风保持关闭；等待 Windows 安排校准声。" :
      "麦克风已就绪，等待 Windows 主控安排校准声。";
  } catch (error) {
    finishCalibration(error.message || "无法启用麦克风");
  }
  update();
});

document.getElementById("enable-speaker").addEventListener("click", async () => {
  try {
    await audio.enable();
    socket.send("AUDIO|1");
    state.notice = state.stream.source === "TONE" ?
      "扬声器已启用，正在播放固定测试音。切换图形程序的音源即可听电脑音乐。" :
      "扬声器已启用，电脑音乐将按主控时间播放。";
  } catch (_) {
    state.notice = "无法启动音频，请检查浏览器权限后再次点击“启用扬声器”。";
  }
  update();
});
document.getElementById("test-click").addEventListener("click", () => {
  socket.send("CLICK");
  state.notice = "已请求在主控时间 +1000 ms 时播放同步提示音。";
  update();
});
function setCalibration(value) {
  if (!Number.isFinite(value)) return;
  audio.calibrationMs = Math.max(-500, Math.min(500, Math.round(value * 10) / 10));
  calibrationInput.value = String(audio.calibrationMs);
  saveCalibration(audio.calibrationMs);
  socket.send("CAL|" + audio.calibrationMs);
  if (state.stream.source === "SYSTEM-ROUTED")
    state.calibrationStatus = musicTimingStatus();
  else if (audio.calibrationMs < 60 - buffer.targetMs)
    state.calibrationStatus = `当前提前 ${Math.abs(audio.calibrationMs)} ms，` +
      `音频包到达后不足 60 ms 可排程；请调回 ${60 - buffer.targetMs} ms 以上，` +
      "否则可能持续断音。";
  update();
}
document.querySelectorAll("[data-cal]").forEach((button) => {
  button.addEventListener("click", () => setCalibration(audio.calibrationMs + Number(button.dataset.cal)));
});
calibrationInput.addEventListener("change", () => setCalibration(Number(calibrationInput.value)));
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    if (state.calibrating) finishCalibration("页面进入后台，校准已取消。");
    socket.send("AUDIO|0");
    buffer.reset();
  } else {
    state.notice = "返回此页面后，请再次点击“启用扬声器”。";
    update();
  }
});
setInterval(() => {
  for (let i = 0; i < 20; i += 1) {
    const packets = buffer.takeReadyBatch(clock, audio.calibrationMs);
    if (!packets) break;
    audio.scheduleBatch(packets, clock);
  }
}, 10);
setInterval(update, 250);
setInterval(() => {
  socket.send("NET|" + (audio.lateCount + buffer.underruns + buffer.dropped));
}, 2000);
update();

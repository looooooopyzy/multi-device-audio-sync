// The Windows shared ClockSyncEngine is the estimator. This adapter owns the
// browser's monotonic T2/T3 stamps and the latest master-supplied affine model.
export class BrowserClockSync {
  constructor() {
    this.model = null;
  }
  handleRequest(fields, receivedPerformanceMs, send) {
    if (fields.length !== 3 || !/^[0-9]+$/.test(fields[1]) || !/^[0-9]+$/.test(fields[2])) return;
    const t2 = Math.round(receivedPerformanceMs * 1e6);
    const t3 = Math.round(performance.now() * 1e6);
    send("SYNC_RESP|" + fields[1] + "|" + fields[2] + "|" + t2 + "|" + t3);
  }
  updateModel(fields) {
    if (fields.length !== 10) return false;
    const values = fields.slice(1, 7).map(Number);
    const count = Number(fields[8]);
    const rawOffsetMs = Number(fields[9]);
    if (!values.every(Number.isFinite) || !Number.isFinite(count) || count < 1 ||
        !Number.isFinite(rawOffsetMs)) return false;
    const [masterMs, clientMs, rate, rttMs, filteredRttMs, jitterMs] = values;
    if (rate < 0.999 || rate > 1.001) return false;
    this.model = { masterMs, clientMs, rate, rttMs, filteredRttMs,
                   jitterMs, quality: fields[7], samples: count,
                   offsetMs: clientMs - masterMs, rawOffsetMs,
                   driftPpm: (rate - 1) * 1e6 };
    return true;
  }
  masterNsToPerformanceMs(masterNs) {
    if (!this.model || !/^[0-9]+$/.test(masterNs)) return null;
    const masterMs = Number(masterNs) / 1e6;
    return this.model.clientMs + (masterMs - this.model.masterMs) * this.model.rate;
  }
  reset() { this.model = null; }
}

export class SpeakerSocket {
  constructor(identity, onMessage, onState) {
    this.identity = identity;
    this.onMessage = onMessage;
    this.onState = onState;
    this.socket = null;
    this.retry = null;
    this.heartbeat = null;
    this.connect();
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden && (!this.socket || this.socket.readyState === WebSocket.CLOSED)) this.connect();
    });
  }
  connect() {
    if (this.socket && (this.socket.readyState === WebSocket.OPEN ||
                        this.socket.readyState === WebSocket.CONNECTING)) return;
    clearTimeout(this.retry);
    const protocol = location.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(protocol + "//" + location.host + "/ws");
    socket.binaryType = "arraybuffer";
    this.socket = socket;
    this.onState("CONNECTING");
    socket.onopen = () => {
      this.onState("CONNECTED");
      this.send("HELLO|" + this.identity.id + "|" + this.identity.name);
      clearInterval(this.heartbeat);
      this.heartbeat = setInterval(() => this.send("PING"), 1000);
    };
    socket.onmessage = (event) => this.onMessage(event.data, performance.now());
    socket.onclose = () => {
      clearInterval(this.heartbeat);
      this.onState("DISCONNECTED");
      this.retry = setTimeout(() => this.connect(), 2000);
    };
    socket.onerror = () => this.onState("NETWORK ERROR");
  }
  send(text) {
    if (this.socket && this.socket.readyState === WebSocket.OPEN) this.socket.send(text);
  }
}

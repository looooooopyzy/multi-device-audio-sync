"""本地 HTTPS/WSS 入口冒烟测试；需要 tools/requirements.txt。"""
import base64
import os
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = Path(sys.argv[1])


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


backend_port, https_port = free_port(), free_port()
master = subprocess.Popen(
    [str(MASTER), "--web-only", "--http-port", str(backend_port)],
    cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
proxy = None
try:
    deadline = time.monotonic() + 8
    while True:
        try:
            with socket.create_connection(("127.0.0.1", backend_port), timeout=1):
                break
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)
    proxy = subprocess.Popen(
        [sys.executable, str(ROOT / "tools/secure_web.py"), "--ip", "127.0.0.1",
         "--port", str(https_port), "--backend-port", str(backend_port)],
        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    ca = ROOT / ".local-certs/lan-root-ca.crt"
    deadline = time.monotonic() + 10
    while not ca.exists() and time.monotonic() < deadline:
        time.sleep(0.1)
    while True:
        try:
            context = ssl.create_default_context(cafile=str(ca))
            connection = context.wrap_socket(socket.create_connection(
                ("127.0.0.1", https_port), timeout=2), server_hostname="127.0.0.1")
            break
        except (OSError, ssl.SSLError):
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)
    with connection:
        connection.settimeout(4)
        connection.sendall(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
        assert b"HTTP/1.1 200 OK" in connection.recv(8192)
    with context.wrap_socket(socket.create_connection(
            ("127.0.0.1", https_port), timeout=2), server_hostname="127.0.0.1") as connection:
        key = base64.b64encode(os.urandom(16)).decode()
        connection.sendall(("GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key +
                            "\r\n\r\n").encode())
        assert b"HTTP/1.1 101 Switching Protocols" in connection.recv(8192)
    print("HTTPS 页面和 WSS 握手通过")
finally:
    if proxy:
        proxy.terminate()
        proxy.wait(timeout=5)
    master.terminate()
    master.wait(timeout=5)

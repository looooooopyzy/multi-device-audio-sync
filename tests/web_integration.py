"""End-to-end Phase A transport check using Python's standard library."""
import base64
import os
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/windows-master/windows-master.exe"


def get(port, path):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as connection:
        connection.sendall(("GET " + path + " HTTP/1.1\r\nHost: localhost\r\n"
                            "Connection: close\r\n\r\n").encode())
        output = bytearray()
        while True:
            part = connection.recv(8192)
            if not part:
                return bytes(output)
            output.extend(part)


def exact(connection, count):
    output = bytearray()
    while len(output) < count:
        part = connection.recv(count - len(output))
        if not part:
            raise RuntimeError("WebSocket closed")
        output.extend(part)
    return bytes(output)


def send_text(connection, message):
    body = message.encode("ascii")
    mask = os.urandom(4)
    header = bytearray([0x81])
    if len(body) < 126:
        header.append(0x80 | len(body))
    else:
        header.extend([0x80 | 126, len(body) >> 8, len(body) & 255])
    connection.sendall(header + mask + bytes(
        value ^ mask[index % 4] for index, value in enumerate(body)))


def receive_text(connection):
    first, second = exact(connection, 2)
    length = second & 127
    if length == 126:
        length = struct.unpack("!H", exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", exact(connection, 8))[0]
    if second & 128 or first & 15 != 1:
        raise AssertionError("expected an unmasked text frame")
    return exact(connection, length).decode("ascii")


with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
process = subprocess.Popen(
    [str(MASTER), "--web-only", "--http-port", str(port)],
    cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
)
try:
    deadline = time.monotonic() + 8
    while True:
        try:
            page = get(port, "/")
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise RuntimeError("master did not start")
            time.sleep(0.1)
    assert page.startswith(b"HTTP/1.1 200 OK") and "启用扬声器".encode() in page
    assert get(port, "/src/app.js").startswith(b"HTTP/1.1 200 OK")
    assert b"audio-session" in page
    assert get(port, "/src/audio-session.js").startswith(b"HTTP/1.1 200 OK")
    assert get(port, "/setup.html?https=17901").startswith(b"HTTP/1.1 200 OK")
    assert get(port, "/src/calibration-recorder-worklet.js").startswith(b"HTTP/1.1 200 OK")
    assert get(port, "/../secret").startswith(b"HTTP/1.1 404 Not Found")

    with socket.create_connection(("127.0.0.1", port), timeout=4) as connection:
        connection.settimeout(4)
        key = base64.b64encode(os.urandom(16)).decode()
        connection.sendall(("GET /ws HTTP/1.1\r\nHost: localhost\r\n"
                            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key +
                            "\r\n\r\n").encode())
        header = bytearray()
        while b"\r\n\r\n" not in header:
            header.extend(connection.recv(1))
        assert header.startswith(b"HTTP/1.1 101 Switching Protocols")
        send_text(connection, "HELLO|123456789|Python Browser")
        samples = 0
        clicked = False
        welcome = False
        end = time.monotonic() + 10
        while time.monotonic() < end:
            message = receive_text(connection)
            parts = message.split("|")
            if parts[0] == "WELCOME":
                welcome = parts[1] == "Windows-Master"
                send_text(connection, "CAL|12.5")
                send_text(connection, "AUDIO|1")
            elif parts[0] == "SYNC_REQ":
                t2 = time.perf_counter_ns() + 7_000_000
                send_text(connection, "SYNC_RESP|" + parts[1] + "|" + parts[2] +
                          "|" + str(t2) + "|" + str(time.perf_counter_ns() + 7_000_000))
            elif parts[0] == "SYNC_MODEL":
                samples = max(samples, int(parts[8]))
                if samples >= 8 and not clicked:
                    send_text(connection, "CLICK")
                    clicked = True
            elif parts[0] == "CLICK_AT":
                target = int(parts[1])
                assert target > time.perf_counter_ns()
                assert parts[2:] == ["10", "1000"]
                break
        else:
            raise AssertionError("no scheduled click from master")
        assert welcome and samples >= 8
        print("HTTP, WebSocket registration, clock samples, calibration and CLICK_AT passed")
finally:
    process.terminate()
    output, _ = process.communicate(timeout=5)
    if process.returncode not in (0, 1, 3221225786, -15):
        print(output.decode(errors="replace")[-2000:])

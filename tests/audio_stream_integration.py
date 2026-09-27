"""Check binary PCM transport and its presentation timeline with a tone source."""
import base64
import os
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MASTER = Path(sys.argv[1])
SOURCE = os.environ.get("MDA_TEST_SOURCE", "tone")
assert SOURCE in ("tone", "system")


def exact(sock, count):
    result = bytearray()
    while len(result) < count:
        part = sock.recv(count - len(result))
        if not part:
            raise RuntimeError("socket closed")
        result.extend(part)
    return bytes(result)


def send_text(sock, value):
    data = value.encode("ascii")
    mask = os.urandom(4)
    header = bytearray([0x81])
    if len(data) < 126:
        header.append(0x80 | len(data))
    else:
        header.extend([0x80 | 126, len(data) >> 8, len(data) & 255])
    sock.sendall(header + mask + bytes(byte ^ mask[index % 4]
                                      for index, byte in enumerate(data)))


def receive_frame(sock):
    first, second = exact(sock, 2)
    length = second & 127
    if length == 126:
        length = struct.unpack("!H", exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", exact(sock, 8))[0]
    if second & 128:
        raise AssertionError("server frame was masked")
    return first & 15, exact(sock, length)


with socket.socket() as reserved:
    reserved.bind(("127.0.0.1", 0))
    port = reserved.getsockname()[1]
process = subprocess.Popen(
    [str(MASTER), "--web-only", "--http-port", str(port), "--source", SOURCE],
    cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
)
try:
    deadline = time.monotonic() + 8
    while True:
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=3)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise RuntimeError("master did not start")
            time.sleep(0.1)
    with connection:
        connection.settimeout(6)
        key = base64.b64encode(os.urandom(16)).decode()
        connection.sendall(("GET /ws HTTP/1.1\r\nHost: localhost\r\n"
                            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key +
                            "\r\n\r\n").encode())
        response = bytearray()
        while b"\r\n\r\n" not in response:
            response.extend(connection.recv(1))
        assert response.startswith(b"HTTP/1.1 101")
        send_text(connection, "HELLO|91919191|Audio Test")
        send_text(connection, "AUDIO|1")
        stream_id = None
        packets = []
        deadline = time.monotonic() + 9
        while time.monotonic() < deadline and len(packets) < 25:
            opcode, body = receive_frame(connection)
            if opcode == 1:
                parts = body.decode("ascii").split("|")
                if parts[0] == "STREAM_INFO":
                    stream_id = int(parts[1])
                    assert parts[2:] == [SOURCE, "48000", "2", "200"]
                elif parts[0] == "SYNC_REQ":
                    now = time.perf_counter_ns() + 8_000_000
                    send_text(connection, "SYNC_RESP|" + parts[1] + "|" + parts[2] +
                              "|" + str(now) + "|" + str(time.perf_counter_ns() + 8_000_000))
            elif opcode == 2:
                assert len(body) == 48 + 480 * 2 * 4
                header = struct.unpack("!IHHIIQqIHHII", body[:48])
                magic, version, size, packet_stream, sequence, frame, target, rate, channels, fmt, count, flags = header
                assert (magic, version, size, rate, channels, fmt, count) == (0x4d444150, 1, 48, 48000, 2, 1, 480)
                assert packet_stream == stream_id and target > time.perf_counter_ns()
                samples = struct.unpack("<960f", body[48:])
                assert max(samples) > 0.1 and min(samples) < -0.1
                packets.append((sequence, frame, target, flags))
        assert len(packets) >= 25, f"received {len(packets)} PCM packets"
        for prior, following in zip(packets, packets[1:]):
            assert following[0] == prior[0] + 1
            assert following[1] == prior[1] + 480
            assert 5_000_000 < following[2] - prior[2] < 20_000_000
        send_text(connection, "CAL|-237")
        increased_delay = None
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and increased_delay is None:
            opcode, body = receive_frame(connection)
            if opcode == 1:
                parts = body.decode("ascii").split("|")
                if parts[0] == "STREAM_INFO" and int(parts[1]) != stream_id:
                    increased_delay = int(parts[5])
                elif parts[0] == "SYNC_REQ":
                    now = time.perf_counter_ns() + 8_000_000
                    send_text(connection, "SYNC_RESP|" + parts[1] + "|" + parts[2] +
                              "|" + str(now) + "|" + str(time.perf_counter_ns() + 8_000_000))
        assert increased_delay >= 337, f"negative calibration lead was {increased_delay} ms"
        print("25 binary PCM packets: format, sequence, sample frame and future timeline passed")
finally:
    process.terminate()
    output, _ = process.communicate(timeout=5)
    if process.returncode not in (0, 1, 3221225786, -15):
        print(output.decode(errors="replace")[-2000:])

"""Measure the live PCM transport without storing the captured music."""
import argparse
import asyncio
import math
import ssl
import struct
import time

import websockets


async def measure(url: str, seconds: float, ca_file: str | None) -> None:
    sequences = []
    frames = []
    presentations = []
    peaks = []
    rms_sum = 0.0
    samples = 0
    clipped = 0
    discontinuities = 0
    stream_id = None
    ssl_context = ssl.create_default_context(cafile=ca_file) if ca_file else None
    async with websockets.connect(url, max_size=1 << 20, ssl=ssl_context) as socket:
        await socket.send("HELLO|11235813213455|Stream diagnostics")
        await socket.send("AUDIO|1")
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                message = await asyncio.wait_for(socket.recv(), timeout=1)
            except asyncio.TimeoutError:
                continue
            if isinstance(message, str):
                fields = message.split("|")
                if fields[0] == "SYNC_REQ" and len(fields) == 3:
                    t2 = time.perf_counter_ns()
                    t3 = time.perf_counter_ns()
                    await socket.send(f"SYNC_RESP|{fields[1]}|{fields[2]}|{t2}|{t3}")
                continue
            if len(message) != 3888:
                continue
            sid, sequence = struct.unpack_from(">II", message, 8)
            if stream_id is None:
                stream_id = sid
            if sid != stream_id:
                continue
            frame, presentation = struct.unpack_from(">QQ", message, 16)
            flag = struct.unpack_from(">I", message, 44)[0]
            values = struct.unpack_from("<960f", message, 48)
            sequences.append(sequence)
            frames.append(frame)
            presentations.append(presentation)
            peak = max(map(abs, values))
            peaks.append(peak)
            rms_sum += sum(value * value for value in values)
            samples += len(values)
            clipped += sum(abs(value) >= 0.999 for value in values)
            discontinuities += bool(flag & 1)
    gaps = sum(max(0, b - a - 1) for a, b in zip(sequences, sequences[1:]))
    frame_gaps = sum(b != a + 480 for a, b in zip(frames, frames[1:]))
    timestamp_jumps = sum(abs((b - a) / 1e6 - 10) > 2 for a, b in
                          zip(presentations, presentations[1:]))
    print(f"packets={len(sequences)} sequence_gaps={gaps} frame_gaps={frame_gaps} "
          f"timestamp_jumps={timestamp_jumps} discontinuities={discontinuities}")
    print(f"peak={max(peaks, default=0):.3f} rms={math.sqrt(rms_sum / samples) if samples else 0:.3f} "
          f"clipped_samples={clipped}/{samples}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="ws://127.0.0.1:17891/ws")
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--ca", help="trusted LAN CA certificate for a wss URL")
    args = parser.parse_args()
    asyncio.run(measure(args.url, args.seconds, args.ca))

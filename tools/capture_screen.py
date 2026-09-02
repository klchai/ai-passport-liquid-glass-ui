#!/usr/bin/env python3
"""Capture the active AI Passport screen over FAP_SCREENSHOT_V1."""

from __future__ import annotations

import argparse
import json
import struct
import time
import zlib
from pathlib import Path
from typing import BinaryIO


COMMAND = b"FAP_SCREENSHOT_V1\n"
MARKER = "FAP_SCREENSHOT_V1"
MAX_PAYLOAD = 240 * 320 * 2
INITIAL_COMMAND_DELAY = 2.0
COMMAND_RETRY_INTERVAL = 5.0


class CaptureError(RuntimeError):
    pass


def parse_header(line: bytes) -> tuple[int, int, int]:
    try:
        marker, width_text, height_text, encoding, length_text = line.decode("ascii").strip().split()
        width = int(width_text)
        height = int(height_text)
        length = int(length_text)
    except (UnicodeDecodeError, ValueError) as error:
        raise CaptureError("invalid screenshot header") from error
    if marker != MARKER or encoding != "RGB565LE":
        raise CaptureError("unsupported screenshot response")
    if width <= 0 or height <= 0 or length != width * height * 2 or length > MAX_PAYLOAD:
        raise CaptureError("invalid screenshot dimensions or payload length")
    return width, height, length


def read_exact(stream: BinaryIO, length: int, deadline: float) -> bytes:
    payload = bytearray()
    while len(payload) < length:
        chunk = stream.read(length - len(payload))
        if not chunk:
            if time.monotonic() < deadline:
                continue
            raise CaptureError(
                f"screenshot timed out at {len(payload)} of {length} bytes"
            )
        payload.extend(chunk)
    return bytes(payload)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def rgb565le_to_png(width: int, height: int, raw: bytes) -> bytes:
    if len(raw) != width * height * 2:
        raise CaptureError("RGB565 payload length does not match the image dimensions")
    scanlines = bytearray()
    offset = 0
    for _ in range(height):
        scanlines.append(0)
        for _ in range(width):
            pixel = raw[offset] | (raw[offset + 1] << 8)
            offset += 2
            scanlines.extend((
                ((pixel >> 11) & 0x1F) * 255 // 31,
                ((pixel >> 5) & 0x3F) * 255 // 63,
                (pixel & 0x1F) * 255 // 31,
            ))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
        + png_chunk(b"IEND", b"")
    )


def serial_module():
    try:
        import serial
        import serial.tools.list_ports
    except ImportError as error:
        raise CaptureError("pyserial is required; activate the ESP-IDF environment first") from error
    return serial


def available_ports() -> list[dict[str, str]]:
    serial = serial_module()
    return [
        {
            "device": item.device,
            "description": item.description or "",
            "manufacturer": item.manufacturer or "",
        }
        for item in serial.tools.list_ports.comports()
    ]


def choose_port(requested: str | None) -> str:
    if requested:
        return requested
    candidates = [
        item["device"] for item in available_ports()
        if "Espressif" in item["manufacturer"] or "JTAG" in item["description"]
    ]
    if len(candidates) == 1:
        return candidates[0]
    raise CaptureError("could not select one ESP32 port automatically; pass --port")


def sequence_output_path(output: Path, index: int, count: int) -> Path:
    if count == 1:
        return output
    return output.with_name(f"{output.stem}-{index + 1:02d}{output.suffix}")


def request_frame(
    stream: BinaryIO,
    timeout: float,
    initial_delay: float = 0,
) -> tuple[int, int, bytes]:
    deadline = time.monotonic() + timeout
    header = None
    next_command = time.monotonic() + initial_delay
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_command:
            stream.write(COMMAND)
            stream.flush()
            next_command = now + COMMAND_RETRY_INTERVAL
        line = stream.readline()
        if line.startswith(MARKER.encode("ascii") + b" "):
            header = line
            break
    if header is None:
        raise CaptureError("device did not answer FAP_SCREENSHOT_V1")
    width, height, length = parse_header(header)
    return width, height, read_exact(stream, length, deadline)


def capture(
    port: str,
    output: Path,
    timeout: float,
    initial_delay: float = INITIAL_COMMAND_DELAY,
) -> dict[str, object]:
    serial = serial_module()
    try:
        with serial.Serial(port, baudrate=115200, timeout=1, write_timeout=3) as stream:
            stream.reset_input_buffer()
            # Opening USB Serial/JTAG can reset the ESP32-C3. The screenshot
            # worker starts after display/audio/battery initialization, so an
            # eager one-shot command is lost during boot. Delay the first send
            # and retry until the protocol header is observed.
            width, height, raw = request_frame(stream, timeout, initial_delay)
    except CaptureError:
        raise
    except (OSError, ValueError) as error:
        raise CaptureError(f"serial capture failed on {port}: {error}") from error

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(rgb565le_to_png(width, height, raw))
    return {
        "ok": True,
        "path": str(output.resolve()),
        "port": port,
        "width": width,
        "height": height,
    }


def capture_sequence(
    port: str,
    output: Path,
    count: int,
    interval: float,
    timeout: float,
    initial_delay: float = INITIAL_COMMAND_DELAY,
) -> dict[str, object]:
    if count <= 0 or interval < 0:
        raise CaptureError("count must be positive and interval non-negative")
    serial = serial_module()
    frames: list[dict[str, object]] = []
    try:
        with serial.Serial(port, baudrate=115200, timeout=1, write_timeout=3) as stream:
            stream.reset_input_buffer()
            for index in range(count):
                if index > 0 and interval > 0:
                    time.sleep(interval)
                width, height, raw = request_frame(
                    stream, timeout, initial_delay if index == 0 else 0)
                target = sequence_output_path(output, index, count)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(rgb565le_to_png(width, height, raw))
                frames.append({
                    "index": index + 1,
                    "path": str(target.resolve()),
                    "width": width,
                    "height": height,
                })
    except CaptureError:
        raise
    except (OSError, ValueError) as error:
        raise CaptureError(f"serial capture failed on {port}: {error}") from error
    return {"ok": True, "port": port, "frames": frames}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial device; auto-detected when exactly one ESP32 is connected")
    parser.add_argument("--output", type=Path, default=Path("build/screen-capture.png"))
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument(
        "--initial-delay",
        type=float,
        default=INITIAL_COMMAND_DELAY,
        help="seconds to wait after opening the serial port before requesting a frame",
    )
    parser.add_argument(
        "--count", type=int, default=1,
        help="number of frames to capture while keeping the serial port open",
    )
    parser.add_argument(
        "--interval", type=float, default=0,
        help="seconds to wait between completed frames when --count is greater than one",
    )
    parser.add_argument("--list-ports", action="store_true")
    args = parser.parse_args()
    try:
        if args.list_ports:
            print(json.dumps({"ok": True, "ports": available_ports()}, indent=2))
            return 0
        if (args.timeout <= 0 or args.initial_delay < 0 or args.count <= 0 or
                args.interval < 0 or args.output.suffix.lower() != ".png"):
            raise CaptureError(
                "timeout/count must be positive, delays non-negative, and output must end in .png"
            )
        port = choose_port(args.port)
        if args.count > 1:
            result = capture_sequence(
                port, args.output, args.count, args.interval,
                args.timeout, args.initial_delay)
        else:
            result = capture(
                port, args.output, args.timeout, args.initial_delay)
        print(json.dumps(result, indent=2))
        return 0
    except CaptureError as error:
        print(json.dumps({"ok": False, "error": str(error)}, indent=2))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

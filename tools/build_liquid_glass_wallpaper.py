#!/usr/bin/env python3
"""Build the display-native Liquid Glass wallpaper with static overlays baked in."""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
from pathlib import Path


WIDTH = 240
HEIGHT = 320
TOP_SHADOW_HEIGHT = 92
GLOBAL_TINT = (0x03, 0x10, 0x1A)
GLOBAL_TINT_OPACITY = 51  # LV_OPA_20
TOP_SHADOW = (0x02, 0x06, 0x0B)
TOP_SHADOW_OPACITY = 127  # LV_OPA_50 at y=0, fading to zero


def blend(background: int, foreground: int, opacity: int) -> int:
    """Return an 8-bit source-over blend using LVGL-compatible 0..255 opacity."""
    return (foreground * opacity + background * (255 - opacity) + 127) // 255


def rgb565le_to_rgb888(raw: bytes) -> bytearray:
    expected = WIDTH * HEIGHT * 2
    if len(raw) != expected:
        raise ValueError(f"expected {expected} RGB565 bytes, got {len(raw)}")

    output = bytearray(WIDTH * HEIGHT * 3)
    for pixel_index, (pixel,) in enumerate(struct.iter_unpack("<H", raw)):
        output[pixel_index * 3] = ((pixel >> 11) & 0x1F) * 255 // 31
        output[pixel_index * 3 + 1] = ((pixel >> 5) & 0x3F) * 255 // 63
        output[pixel_index * 3 + 2] = (pixel & 0x1F) * 255 // 31
    return output


def rgb888_to_rgb565le(rgb: bytes) -> bytes:
    output = bytearray(WIDTH * HEIGHT * 2)
    for pixel_index in range(WIDTH * HEIGHT):
        offset = pixel_index * 3
        red, green, blue = rgb[offset:offset + 3]
        pixel = (
            ((red * 31 + 127) // 255 << 11)
            | ((green * 63 + 127) // 255 << 5)
            | ((blue * 31 + 127) // 255)
        )
        struct.pack_into("<H", output, pixel_index * 2, pixel)
    return bytes(output)


def render_source(source: Path) -> bytes:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required to resize the wallpaper source")
    command = [
        ffmpeg, "-v", "error", "-i", str(source),
        "-vf", f"scale={WIDTH}:{HEIGHT}:flags=lanczos",
        "-frames:v", "1", "-pix_fmt", "rgb565le",
        "-f", "rawvideo", "pipe:1",
    ]
    result = subprocess.run(command, check=True, capture_output=True)
    return result.stdout


def bake_overlays(raw: bytes) -> bytes:
    rgb = rgb565le_to_rgb888(raw)
    for y in range(HEIGHT):
        shadow_opacity = 0
        if y < TOP_SHADOW_HEIGHT:
            shadow_opacity = (
                TOP_SHADOW_OPACITY * (TOP_SHADOW_HEIGHT - 1 - y)
                + (TOP_SHADOW_HEIGHT - 1) // 2
            ) // (TOP_SHADOW_HEIGHT - 1)

        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 3
            for channel in range(3):
                value = blend(
                    rgb[offset + channel],
                    GLOBAL_TINT[channel],
                    GLOBAL_TINT_OPACITY,
                )
                if shadow_opacity:
                    value = blend(
                        value,
                        TOP_SHADOW[channel],
                        shadow_opacity,
                    )
                rgb[offset + channel] = value
    return rgb888_to_rgb565le(rgb)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=root / "assets/images/liquid_glass_wallpaper_source.jpg",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "assets/images/liquid_glass_wallpaper.rgb565",
    )
    args = parser.parse_args()

    output = bake_overlays(render_source(args.source))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {len(output)} bytes to {args.output}")


if __name__ == "__main__":
    main()

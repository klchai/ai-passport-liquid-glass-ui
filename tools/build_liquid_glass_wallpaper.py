#!/usr/bin/env python3
"""Build the display-native graphite wallpaper and its rim color samples.

The default source is an original procedural scene: satin folds of cool
neutral light rising from the lower left over a graphite field. It is rendered
at 720x960 with 16-bit precision, downscaled to the 240x320 panel with
ffmpeg's lanczos filter, given the static neutral tint and the
title-readability shadow, and quantized to RGB565 with a 4x4 ordered dither.
Without the dither the dark gradients band visibly in RGB565's 5- and 6-bit
channels.

The dithered raster uses far fewer than 256 distinct RGB565 colors (137 for
the default scene), so it is stored losslessly as an "LGP8" indexed image: a
256-entry RGB565 palette followed by one palette index per pixel (see
main/liquid_glass_compositor_core.h for the byte layout). That halves the
bytes the firmware streams from Flash on every redraw. A raster with more than
256 colors is rejected rather than quantized again. `--from-rgb565` re-encodes
an existing 240x320 little-endian RGB565 raster without re-rendering (and
without ffmpeg).

The script also prints the WALLPAPER_CENTER_SAMPLES rows that
main/ui_glass_optics.c uses to borrow background color at glass rims.
`--check-samples` compares those rows with the committed asset using integer
math only, so the static CI job can run it without ffmpeg.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "assets/images/liquid_glass_wallpaper.lgp8"
OPTICS_SOURCE = ROOT / "main/ui_glass_optics.c"

WIDTH = 240
HEIGHT = 320
SOURCE_WIDTH = WIDTH * 3
SOURCE_HEIGHT = HEIGHT * 3
# One display pixel in the scene's height-normalized units. Highlights
# narrower than about 1.2 display pixels vanish in the 3x downscale.
PX = 1.0 / HEIGHT

BASE_TOP = (0.150, 0.157, 0.172)     # graphite, slightly cool
BASE_BOTTOM = (0.185, 0.192, 0.208)
LIGHT = (0.92, 0.94, 0.97)           # cool neutral highlight

TOP_SHADOW_HEIGHT = 92
GLOBAL_TINT = (0x0B, 0x0C, 0x0E)
GLOBAL_TINT_OPACITY = 51  # LV_OPA_20
TOP_SHADOW = (0x05, 0x06, 0x07)
TOP_SHADOW_OPACITY = 127  # LV_OPA_50 at y=0, fading to zero
BAYER_4X4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))

# ui_glass_background_at_y() interpolates SAMPLE_COUNT colors evenly spaced
# over the panel height, each averaged from a 16x5 patch on the center line.
SAMPLE_COUNT = 20
SAMPLE_HALF_WIDTH = 8
SAMPLE_HALF_HEIGHT = 2


def fold(x: float, y: float, y0: float, slope: float, amplitude: float,
         frequency: float, phase: float) -> float:
    """Signed distance from the curve y = y0 + slope*x + a*sin(f*x + p)."""
    curve = y0 + slope * x + amplitude * math.sin(frequency * x + phase)
    gradient = slope + amplitude * frequency * math.cos(frequency * x + phase)
    return (y - curve) / math.sqrt(1.0 + gradient * gradient)


def lighting(distance: float, edge: float, edge_width: float, sheet: float,
             sheet_width: float, shadow: float, shadow_width: float) -> float:
    """A bright edge at the fold, its lit sheet below and a shadow above."""
    value = edge * math.exp(-(distance / edge_width) ** 2)
    if distance > 0:
        value += sheet * math.exp(-distance / sheet_width)
    else:
        value -= shadow * math.exp(-(distance / shadow_width) ** 2)
    return value


def scene(x: float, y: float) -> float:
    """Light added at (x, y), both in height-normalized units.

    Two satin folds rise toward the right edge so their lit sheets sit behind
    the lower half of each page and the persistent footer, giving the glass
    there something to transmit. A faint third fold crosses the upper half; the top-left title
    area stays calm.
    """
    sheen = 0.70 + 0.30 * math.sin(5.0 * x - 3.0 * y + 0.8)
    value = lighting(fold(x, y, 0.80, -0.36, 0.030, 5.5, 0.9),
                     0.62 * sheen, 1.3 * PX, 0.22, 40 * PX, 0.10, 8 * PX)
    value += lighting(fold(x, y, 1.03, -0.30, 0.025, 6.5, 2.4),
                      0.70, 1.4 * PX, 0.26, 55 * PX, 0.10, 8 * PX)
    value += lighting(fold(x, y, 0.20, 0.52, 0.015, 4.0, 1.7),
                      0.22 * sheen, 1.1 * PX, 0.06, 30 * PX, 0.04, 6 * PX)
    return value


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_generated_source(path: Path) -> None:
    """Render the scene to a 16-bit RGB PNG at three times panel size."""
    rows = bytearray()
    for py in range(SOURCE_HEIGHT):
        y = (py + 0.5) / SOURCE_HEIGHT
        rows.append(0)
        for px in range(SOURCE_WIDTH):
            x = (px + 0.5) / SOURCE_HEIGHT  # height units keep folds unskewed
            u = (px + 0.5) / SOURCE_WIDTH
            vignette = 1.0 - 0.45 * ((u - 0.55) ** 2 * 1.6 + (y - 0.62) ** 2)
            light = scene(x, y)
            for channel in range(3):
                base = (BASE_TOP[channel]
                        + (BASE_BOTTOM[channel] - BASE_TOP[channel]) * y)
                value = (base + light * LIGHT[channel]) * vignette
                value = min(1.0, max(0.0, value))
                rows += struct.pack(">H", int(value * 65535 + 0.5))
    header = struct.pack(">IIBBBBB", SOURCE_WIDTH, SOURCE_HEIGHT, 16, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
                     + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
                     + png_chunk(b"IEND", b""))


def downscale(source: Path) -> list[float]:
    """Resize any image to the panel as 0..1 floats, keeping 16-bit precision."""
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required to resize the wallpaper source")
    command = [
        ffmpeg, "-v", "error", "-i", str(source),
        "-vf", f"scale={WIDTH}:{HEIGHT}:flags=lanczos",
        "-frames:v", "1", "-pix_fmt", "rgb48le",
        "-f", "rawvideo", "pipe:1",
    ]
    raw = subprocess.run(command, check=True, capture_output=True).stdout
    return [value / 65535.0
            for value in struct.unpack(f"<{WIDTH * HEIGHT * 3}H", raw)]


def bake_and_dither(rgb: list[float]) -> bytes:
    """Apply the static overlays, then ordered-dither to RGB565LE."""
    tint_opacity = GLOBAL_TINT_OPACITY / 255.0
    output = bytearray(WIDTH * HEIGHT * 2)
    for y in range(HEIGHT):
        shadow = 0.0
        if y < TOP_SHADOW_HEIGHT:
            shadow = (TOP_SHADOW_OPACITY * (TOP_SHADOW_HEIGHT - 1 - y)
                      / (TOP_SHADOW_HEIGHT - 1) / 255.0)
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 3
            threshold = (BAYER_4X4[y & 3][x & 3] + 0.5) / 16.0
            levels = []
            for channel, maximum in enumerate((31, 63, 31)):
                value = rgb[offset + channel]
                value = (value * (1 - tint_opacity)
                         + GLOBAL_TINT[channel] / 255.0 * tint_opacity)
                value = (value * (1 - shadow)
                         + TOP_SHADOW[channel] / 255.0 * shadow)
                levels.append(min(maximum, int(value * maximum + threshold)))
            pixel = levels[0] << 11 | levels[1] << 5 | levels[2]
            struct.pack_into("<H", output, (y * WIDTH + x) * 2, pixel)
    return bytes(output)


INDEXED_MAGIC = b"LGP8"
PALETTE_SIZE = 256
INDEXED_HEADER = struct.Struct("<4sHHHH")


def encode_indexed(rgb565: bytes, width: int = WIDTH,
                   height: int = HEIGHT) -> bytes:
    """Store a little-endian RGB565 raster losslessly as an LGP8 image.

    The palette is sorted by RGB565 value so the same raster always produces
    the same bytes.
    """
    if len(rgb565) != width * height * 2:
        raise ValueError(f"expected {width * height * 2} bytes of RGB565, "
                         f"got {len(rgb565)}")
    pixels = struct.unpack(f"<{width * height}H", rgb565)
    palette = sorted(set(pixels))
    if len(palette) > PALETTE_SIZE:
        raise ValueError(f"the wallpaper uses {len(palette)} RGB565 colors; "
                         f"the indexed format holds at most {PALETTE_SIZE}")
    index_of = {color: index for index, color in enumerate(palette)}
    padded = palette + [0] * (PALETTE_SIZE - len(palette))
    return (INDEXED_HEADER.pack(INDEXED_MAGIC, width, height, len(palette), 0)
            + struct.pack(f"<{PALETTE_SIZE}H", *padded)
            + bytes(index_of[pixel] for pixel in pixels))


def decode_indexed(asset: bytes) -> list[int]:
    """Return the RGB565 pixels of an LGP8 image, validating its layout."""
    if len(asset) < INDEXED_HEADER.size + PALETTE_SIZE * 2:
        raise ValueError("truncated LGP8 image")
    magic, width, height, colors, _ = INDEXED_HEADER.unpack_from(asset)
    if magic != INDEXED_MAGIC:
        raise ValueError(f"not an LGP8 image (magic {magic!r})")
    if (width, height) != (WIDTH, HEIGHT):
        raise ValueError(f"expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    if not 0 < colors <= PALETTE_SIZE:
        raise ValueError(f"invalid palette size {colors}")
    offset = INDEXED_HEADER.size + PALETTE_SIZE * 2
    if len(asset) != offset + width * height:
        raise ValueError(f"expected {offset + width * height} bytes, "
                         f"got {len(asset)}")
    palette = struct.unpack_from(f"<{PALETTE_SIZE}H", asset,
                                 INDEXED_HEADER.size)
    indices = asset[offset:]
    if max(indices) >= colors:
        raise ValueError("pixel index outside the palette")
    return [palette[index] for index in indices]


def rgb565_to_rgb888(pixel: int) -> tuple[int, int, int]:
    red, green, blue = pixel >> 11, (pixel >> 5) & 0x3F, pixel & 0x1F
    return (red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2)


def center_samples(pixels: list[int]) -> list[int]:
    """Average 16x5 patches on the center line, one per LUT entry."""
    if len(pixels) != WIDTH * HEIGHT:
        raise ValueError(f"expected {WIDTH * HEIGHT} pixels, got {len(pixels)}")
    samples = []
    for index in range(SAMPLE_COUNT):
        center = round(index * (HEIGHT - 1) / (SAMPLE_COUNT - 1))
        colors = [
            rgb565_to_rgb888(pixels[y * WIDTH + x])
            for y in range(max(0, center - SAMPLE_HALF_HEIGHT),
                           min(HEIGHT, center + SAMPLE_HALF_HEIGHT + 1))
            for x in range(WIDTH // 2 - SAMPLE_HALF_WIDTH,
                           WIDTH // 2 + SAMPLE_HALF_WIDTH)
        ]
        count = len(colors)
        red, green, blue = (
            (sum(color[channel] for color in colors) + count // 2) // count
            for channel in range(3)
        )
        samples.append(red << 16 | green << 8 | blue)
    return samples


def format_samples(samples: list[int]) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{value:06X}u" for value in samples[row:row + 5]) + ","
        for row in range(0, len(samples), 5)
    )


def committed_samples() -> list[int]:
    source = OPTICS_SOURCE.read_text(encoding="utf-8")
    match = re.search(r"WALLPAPER_CENTER_SAMPLES\[\] = \{(.*?)\};", source, re.S)
    if not match:
        raise ValueError(f"WALLPAPER_CENTER_SAMPLES not found in {OPTICS_SOURCE}")
    return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{6})u", match.group(1))]


def check_samples() -> int:
    expected = center_samples(decode_indexed(ASSET.read_bytes()))
    actual = committed_samples()
    if actual == expected:
        print("Wallpaper rim samples: PASS")
        return 0
    print(f"{OPTICS_SOURCE.relative_to(ROOT)}: WALLPAPER_CENTER_SAMPLES does not "
          f"match {ASSET.relative_to(ROOT)}; replace the table with:", file=sys.stderr)
    print(format_samples(expected), file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    raster_source = parser.add_mutually_exclusive_group()
    raster_source.add_argument(
        "--source", type=Path,
        help="use this image instead of the generated graphite scene",
    )
    raster_source.add_argument(
        "--from-rgb565", type=Path,
        help="re-encode an existing 240x320 little-endian RGB565 raster "
             "instead of rendering (no ffmpeg needed)",
    )
    parser.add_argument("--output", type=Path, default=ASSET)
    parser.add_argument(
        "--check-samples", action="store_true",
        help="verify main/ui_glass_optics.c against the committed asset",
    )
    args = parser.parse_args()
    if args.check_samples:
        return check_samples()

    if args.from_rgb565 is not None:
        raster = args.from_rgb565.read_bytes()
    else:
        with tempfile.TemporaryDirectory() as scratch:
            source = args.source
            if source is None:
                source = Path(scratch) / "graphite_source.png"
                write_generated_source(source)
            raster = bake_and_dither(downscale(source))
    asset = encode_indexed(raster)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(asset)
    colors = INDEXED_HEADER.unpack_from(asset)[3]
    print(f"wrote {len(asset)} bytes ({colors} colors) to {args.output}")
    print("WALLPAPER_CENTER_SAMPLES for main/ui_glass_optics.c:")
    print(format_samples(center_samples(decode_indexed(asset))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

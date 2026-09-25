#!/usr/bin/env python3
"""Host tests for the indexed (LGP8) wallpaper encoder."""

from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_liquid_glass_wallpaper",
    ROOT / "tools" / "build_liquid_glass_wallpaper.py",
)
assert SPEC and SPEC.loader
WALLPAPER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = WALLPAPER
SPEC.loader.exec_module(WALLPAPER)

PIXELS = WALLPAPER.WIDTH * WALLPAPER.HEIGHT


def raster(colors: list[int]) -> bytes:
    """A panel-sized RGB565LE raster cycling through `colors`."""
    return struct.pack(f"<{PIXELS}H",
                       *(colors[index % len(colors)] for index in range(PIXELS)))


class IndexedWallpaperTest(unittest.TestCase):
    def test_round_trip_is_lossless_and_deterministic(self) -> None:
        source = raster([0xF800, 0x001F, 0x07E0, 0x0000, 0xFFFF])
        encoded = WALLPAPER.encode_indexed(source)
        self.assertEqual(encoded, WALLPAPER.encode_indexed(source))
        self.assertEqual(
            WALLPAPER.decode_indexed(encoded),
            list(struct.unpack(f"<{PIXELS}H", source)),
        )
        magic, width, height, colors, reserved = (
            WALLPAPER.INDEXED_HEADER.unpack_from(encoded))
        self.assertEqual(
            (magic, width, height, colors, reserved),
            (b"LGP8", WALLPAPER.WIDTH, WALLPAPER.HEIGHT, 5, 0),
        )
        # Header, the fixed 256-entry palette, then one byte per pixel.
        self.assertEqual(len(encoded),
                         WALLPAPER.INDEXED_HEADER.size + 512 + PIXELS)

    def test_rejects_more_than_256_colors(self) -> None:
        with self.assertRaisesRegex(ValueError, "at most 256"):
            WALLPAPER.encode_indexed(raster(list(range(300))))

    def test_rejects_malformed_images(self) -> None:
        encoded = WALLPAPER.encode_indexed(raster([0x1234, 0x4321]))
        with self.assertRaises(ValueError):
            WALLPAPER.decode_indexed(encoded[:-1])
        with self.assertRaises(ValueError):
            WALLPAPER.decode_indexed(b"XGP8" + encoded[4:])
        bad_index = bytearray(encoded)
        bad_index[-1] = 7  # only two colors exist
        with self.assertRaisesRegex(ValueError, "outside the palette"):
            WALLPAPER.decode_indexed(bytes(bad_index))

    def test_committed_asset_decodes(self) -> None:
        pixels = WALLPAPER.decode_indexed(WALLPAPER.ASSET.read_bytes())
        self.assertEqual(len(pixels), PIXELS)
        self.assertEqual(WALLPAPER.center_samples(pixels),
                         WALLPAPER.committed_samples())


if __name__ == "__main__":
    unittest.main()

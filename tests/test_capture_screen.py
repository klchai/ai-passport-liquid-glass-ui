#!/usr/bin/env python3

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.capture_screen import (
    CaptureError,
    parse_header,
    read_exact,
    rgb565le_to_png,
    sequence_output_path,
)


class IntermittentStream:
    def __init__(self, chunks: list[bytes]) -> None:
        self.chunks = chunks

    def read(self, _length: int) -> bytes:
        return self.chunks.pop(0)


class CaptureScreenTests(unittest.TestCase):
    def test_parse_header(self) -> None:
        self.assertEqual(
            parse_header(b"FAP_SCREENSHOT_V1 240 320 RGB565LE 153600\n"),
            (240, 320, 153600),
        )

    def test_rejects_inconsistent_length(self) -> None:
        with self.assertRaises(CaptureError):
            parse_header(b"FAP_SCREENSHOT_V1 240 320 RGB565LE 100\n")

    def test_rgb565_png_dimensions(self) -> None:
        png = rgb565le_to_png(2, 1, b"\x00\xf8\xe0\x07")
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (2, 1))

    def test_read_exact_tolerates_a_temporary_empty_read(self) -> None:
        stream = IntermittentStream([b"ab", b"", b"cd"])
        self.assertEqual(read_exact(stream, 4, float("inf")), b"abcd")

    def test_sequence_output_path(self) -> None:
        output = Path("build/showcase.png")
        self.assertEqual(sequence_output_path(output, 0, 1), output)
        self.assertEqual(
            sequence_output_path(output, 2, 8),
            Path("build/showcase-03.png"),
        )


if __name__ == "__main__":
    unittest.main()

<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Images

Store reusable source images and generated display assets here.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Liquid Glass wallpaper

- `liquid_glass_wallpaper.lgp8`: 240 × 320 indexed ("LGP8") firmware asset
  showing graphite satin folds: a 12-byte header, a 256-entry little-endian
  RGB565 palette and one palette index per pixel, 77,324 bytes in total. The
  dithered scene uses 137 colors, so the encoding is lossless;
  `main/liquid_glass_compositor_core.h` documents the byte layout. Generated
  with `python tools/build_liquid_glass_wallpaper.py` (requires `ffmpeg`).
  `--from-rgb565 <raster>` re-encodes an existing 240 × 320 little-endian
  RGB565 raster without `ffmpeg`. A raster with more than 256 colors is
  rejected instead of being quantized again.
- Source: an original procedural scene defined in that script and rendered at
  720 × 960 with 16-bit precision, so the script is the editable source. Pass
  `--source <image>` to build from another image instead.
- Integration: embedded from `main/CMakeLists.txt`.
  `main/ui_glass_compositor.c` validates it once, copies the 512-byte palette
  to RAM and decodes rows from Flash straight into LVGL's draw buffer; no
  full-screen decode buffer is allocated.
- Optimization: the static neutral tint and title-readability gradient are
  baked into the asset so redraws do not alpha-blend two full-screen layers.
  A 4 × 4 ordered dither keeps the dark gradients from banding in RGB565.
  One byte per pixel halves the Flash reads of the previous RGB565 raster.
- Rim colors: the script prints the `WALLPAPER_CENTER_SAMPLES` rows for
  `main/ui_glass_optics.c`. Update them with every new asset;
  `tools/validate.sh` runs `--check-samples` and fails if they drift.
- License: original work, covered by the repository [license](../../LICENSE).

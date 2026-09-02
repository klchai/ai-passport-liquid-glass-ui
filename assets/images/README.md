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

- `liquid_glass_wallpaper_source.jpg`: 720 × 960 source crop used for review and future reprocessing.
- `liquid_glass_wallpaper.rgb565`: 240 × 320 little-endian RGB565 firmware asset; generated with `python tools/build_liquid_glass_wallpaper.py`.
- Integration: embedded from `main/CMakeLists.txt` and rendered directly from Flash by `main/ui_glass.c`; no full-screen decode buffer is allocated.
- Optimization: the static blue tint and title-readability gradient are baked
  into the RGB565 asset so redraws do not alpha-blend two full-screen layers.
- Source: [Unsplash glass wallpaper search](https://unsplash.com/s/photos/glass-wallpaper), image asset `photo-1706101299176-292d8c5e470e`.
- License: [Unsplash License](https://unsplash.com/license).

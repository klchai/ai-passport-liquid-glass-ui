<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fonts

Store reusable font files and generated font sources here.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Montserrat-Medium.ttf

- Source: LVGL's bundled `scripts/built_in_font/Montserrat-Medium.ttf`, the same
  face behind the UI's 14 px and 20 px text. It is tracked here so the digit
  font can be regenerated and verified on a bare checkout, which is what CI's
  static job is; `managed_components/` is gitignored and absent there.
- License: SIL Open Font License 1.1, see `Montserrat-OFL.txt`.
- Consumer: `tools/gen_digit_font.py` subsets `0123456789:-.%BMK` at 44 px,
  4 bpp, uncompressed, into `main/font_digits_44.c` for the Kaboo headline
  number. Regenerate with `python3 tools/gen_digit_font.py`; `--check`, run by
  `tools/validate.sh --static`, fails when the committed file is stale.
- The TTF itself is never compiled into the firmware; only the subset is.

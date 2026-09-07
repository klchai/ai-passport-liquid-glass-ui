#!/usr/bin/env python3
"""Generate the digit-subset headline font used by the Kaboo page.

The repository only compiles Montserrat 14 and 20 (see sdkconfig.defaults), and
neither is large enough for a glanceable headline number on the 240x320 panel.
Enabling a full 44 px Montserrat would add tens of kilobytes of glyphs we never
draw, so we subset to exactly the characters the headline renders.

Source face is LVGL's own Montserrat-Medium.ttf, so the headline matches the
rest of the UI rather than introducing a second typeface.

Usage:
    python3 tools/gen_digit_font.py            # regenerate main/font_digits_44.c
    python3 tools/gen_digit_font.py --check    # verify the checked-in file is current

Requires lv_font_conv (fetched through npx on demand).
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
TTF = REPO / "managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
OUT = REPO / "main/font_digits_44.c"

FONT_NAME = "font_digits_44"
SIZE = 44
BPP = 4

# Everything showcase_scenes.c renders through this face:
#   - token counts such as "9.1B" / "144.6M" / "537.3K" (format_tokens)
#   - the "--" placeholder before the first BLE sync
# The colon and percent sign are kept so the subset can serve a clock or a
# large percentage without regenerating. Any character missing here draws as
# an empty box, so keep this in sync with what is actually rendered.
SYMBOLS = "0123456789:-.%BMK"


def build_command(ttf: pathlib.Path, out: pathlib.Path) -> list[str]:
    # lv_font_conv copies its argv verbatim into the generated header comment,
    # so every path here must be repo-relative: an absolute path would bake the
    # generating machine's home directory into a committed file and make
    # `--check` fail on any other checkout.
    return [
        "npx",
        "--yes",
        "lv_font_conv@1.5.3",
        "--font", str(ttf.relative_to(REPO)),
        "--size", str(SIZE),
        "--bpp", str(BPP),
        "--format", "lvgl",
        "--symbols", SYMBOLS,
        "--lv-include", "lvgl.h",
        "--force-fast-kern-format",
        # Emit uncompressed glyph bitmaps. LVGL is built here without
        # CONFIG_LV_USE_FONT_COMPRESSED, and a compressed font renders as
        # nothing at all rather than failing loudly.
        "--no-compress",
        "-o", str(out.relative_to(REPO)),
    ]


def generate(destination: pathlib.Path) -> None:
    if not TTF.exists():
        sys.exit(f"missing source face: {TTF}")
    if shutil.which("npx") is None:
        sys.exit("npx not found; install Node.js to regenerate the font")

    # lv_font_conv derives BOTH the include-guard macro (FONT_DIGITS_44) and
    # the exported lv_font_t symbol from the `-o` basename, so the scratch
    # file must keep the real basename. Vary only the directory: render into
    # build/fontgen/<same name>, then move the bytes to their destination.
    scratch_dir = REPO / "build" / "fontgen"
    scratch_dir.mkdir(parents=True, exist_ok=True)
    scratch = scratch_dir / OUT.name
    result = subprocess.run(build_command(TTF, scratch), cwd=REPO)
    if result.returncode != 0:
        sys.exit(f"lv_font_conv failed with exit code {result.returncode}")

    # Normalise the header: the recorded `-o` becomes the canonical committed
    # path, and the trailing blank line lv_font_conv emits is dropped, so the
    # output is byte-stable across machines and passes `git diff --check`.
    text = scratch.read_text()
    text = text.replace(
        str(scratch.relative_to(REPO)), OUT.relative_to(REPO).as_posix(), 1
    )
    text = text.rstrip("\n") + "\n"
    destination.write_text(text)
    scratch.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate into a temporary file and diff against the committed font",
    )
    args = parser.parse_args()

    if not args.check:
        generate(OUT)
        print(f"wrote {OUT.relative_to(REPO)} ({OUT.stat().st_size} bytes)")
        return 0

    if not OUT.exists():
        print(f"{OUT.relative_to(REPO)} is missing; run without --check", file=sys.stderr)
        return 1

    # The source face lives in managed_components/, which is gitignored and only
    # present after an ESP-IDF component resolve. A bare checkout (CI's static
    # job, or a fresh clone) therefore cannot regenerate, and treating that as a
    # failure would make the gate fail for a reason unrelated to the font. Skip
    # instead, and say so loudly enough that a silent skip is not mistaken for a
    # pass. The firmware job builds with the components resolved, so a genuinely
    # stale font is still caught there.
    if not TTF.exists():
        print(
            f"SKIP: {TTF.relative_to(REPO)} is absent "
            "(managed_components not resolved); cannot verify font freshness"
        )
        return 0
    if shutil.which("npx") is None:
        print("SKIP: npx unavailable; cannot verify font freshness")
        return 0

    with tempfile.TemporaryDirectory() as tmp:
        candidate = pathlib.Path(tmp) / OUT.name
        generate(candidate)
        if candidate.read_bytes() != OUT.read_bytes():
            print(
                f"{OUT.relative_to(REPO)} is stale; rerun tools/gen_digit_font.py",
                file=sys.stderr,
            )
            return 1

    print(f"{OUT.relative_to(REPO)} is up to date")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

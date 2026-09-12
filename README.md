<p align="right">
  <strong>English</strong> · <a href="README.zh_CN.md">简体中文</a>
</p>

# Liquid Glass UI on ESP32-C3

A ten-page app combining eight interaction scenes and two live usage dashboards
built for the AI Passport
hardware: a 240×320 display, three physical buttons, 8 MB Flash, and no PSRAM.

![Liquid Glass UI on ESP32-C3](design/promo/liquid-glass-showcase-poster-v1-preview.jpg)

This repository explores how translucent materials, focus movement, morphing,
state feedback, and mobile-style navigation can remain expressive under tight
microcontroller constraints. It is an independent design and engineering
experiment and is not affiliated with Apple.

## The reel

| Player | Home | Focus | Controls |
| --- | --- | --- | --- |
| <img src="design/screenshots/01-player.png" width="180" alt="Player scene"> | <img src="design/screenshots/02-home.png" width="180" alt="Home scene"> | <img src="design/screenshots/03-focus.png" width="180" alt="Focus scene"> | <img src="design/screenshots/04-controls.png" width="180" alt="Controls scene"> |
| Devices | Activity | Moments | Appearance |
| <img src="design/screenshots/05-devices.png" width="180" alt="Devices scene"> | <img src="design/screenshots/06-activity.png" width="180" alt="Activity scene"> | <img src="design/screenshots/07-moments.png" width="180" alt="Moments scene"> | <img src="design/screenshots/08-appearance.png" width="180" alt="Appearance scene"> |

The images above show the original eight-scene release. The current app starts
on Player, followed by Home, Focus, Controls, Devices, Activity, Moments,
Appearance, Kaboo, and Claude. Automatic page tours and scene demos are disabled.

| Button | Browse mode | Scene controls |
| --- | --- | --- |
| UP / DOWN | Previous / next page | Move focus or change the current value |
| OK | Run the page action | Activate selection; Controls selects the next row |
| Long OK | Enter scene controls | Return to browsing |

The footer briefly explains long OK when entering a page or switching modes.
Claude has no scene controls. Kaboo supports previous/next cards and pauses
its eight-second rotation while scene controls are active. Its cards show token
usage and cost; Claude shows used quota and reset time. Both expose sample age.
Showcase actions and sample device states are labeled Demo; they do not share
content, connect devices, or put the board to sleep. Controls adjusts real
brightness and volume, and battery readings refresh once per minute.

## What it demonstrates

- Glass surfaces that retain wallpaper detail instead of becoming opaque cards.
- Deterministic non-linear motion for focus, segmented controls, toggles,
  sliders, navigation, and menu morphing.
- Eight mobile-style scenes rather than a conventional embedded dashboard.
- Standard, High Contrast, Reduce Glass, and Reduce Motion profiles.
- An allocation-conscious RGB565 compositor and bounded dirty-region planner.
- Low-memory screenshots over USB Serial/JTAG for repeatable visual review.

## Design handoff

- [Product definition](PRODUCT.md)
- [System design](DESIGN.md)
- [Component inventory](design/liquid-glass/components.json)
- [Design tokens](design/liquid-glass/tokens.json)
- [Design package notes](design/liquid-glass/README.md)

Reusable board logic remains in `components/bsp`; pages, state machines, and
animations remain in `main`. Testable motion, optics, and compositor math are
kept independent from ESP-IDF/LVGL and covered by host tests.

## Build and validate

Requirements:

- AI Passport / ESP32-C3 target hardware
- ESP-IDF 5.5.3

```bash
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh
```

For the smallest feedback loop:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

The firmware preserves the AI Passport template contracts: a 3 MB application
limit, `cardid` at `0x356000`, permanent Recovery at `0x700000`, and the
five-second UP-key Recovery hook.

## Capture the display

```bash
python tools/capture_screen.py \
  --port /dev/cu.usbmodem101 \
  --output build/showcase/frame.png \
  --count 8 \
  --interval 7
```

## Credits and license

Built on the open-source [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
project. Released under the MIT License; see [LICENSE](LICENSE).

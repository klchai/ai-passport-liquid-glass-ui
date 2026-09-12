<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Polished all ten dashboard pages without replacing the glass layout. Mode
  entry/exit hints explain long OK without crowding long page titles. Controls
  show brightness percentages and audio availability; Activity uses consistent
  demo states. Player, Home, Devices, and Moments identify sample content and
  keep action feedback separate from connection state. Focus dims its backdrop
  and uses a checkbox for Auto resume. Accessibility opacity now covers Player,
  Home, and the data pages. Kaboo labels token units, truncates long model names,
  supports previous/next cards, and pauses rotation during scene control.
  Claude labels used quota, and both data pages expose sample age and mute old
  values. Battery readings refresh in the application task once per minute.

- Fixed three showcase layout defects seen on hardware. Player's title and
  album cards now share the 212 px content width; the Quick Actions trigger
  sits 8 px inside the title card instead of 6 px past its edge; the expanded
  menu keeps the trigger's right edge so it still morphs from its own origin;
  its shadow stays inside the semi-transparent surface instead of dropping
  below it as a second outline; and the speaker status lines clear the
  persistent nav bar. Home's dock selection is a control-radius rounded
  rectangle inset 8 px vertically inside the floating-radius dock (22 − 8 =
  14), so the corner arcs run concentric instead of the old pill curving more
  tightly than the dock around it; the 10 px side margins centre the three
  tabs. Focus's segmented indicator is a Regular-material glass surface at accent
  tint, inset by the optic ring count so its edge rings never land on the
  platter's, carrying the control radius and a glint sweep on selection.
- Gave each Claude quota window its own wire flag so a payload carrying only
  one of them is no longer rejected outright. Claude Code emits a window only
  while it is active, and the previous single flag made the missing window's
  zero reset epoch fail validation, discarding the valid Kaboo data in the same
  packet. A window with no data now reads "not active" instead of a fabricated
  0%. The Mac bridge flags a window only when it has both a percentage and a
  usable reset time, matches devices on the service UUID alone (never the
  advertised name, which is not an identity), accepts `--device <address>` to
  pin one board, and survives a dropped connection or an unreadable snapshot
  instead of exiting. A failed advertising restart now retries on a timer;
  previously it only logged, leaving the device permanently undiscoverable
  while the link state machine believed it was advertising.
- Unified showcase rounded rectangles on the Glass System's control, panel,
  and floating radius tokens while retaining explicit circles, capsules, and
  square canvas layers. Repository checks now reject new literal radius values
  in showcase solid and reference-glass objects.
- Merged the eight-scene showcase and the live-data dashboard into one
  ten-page carousel: Player, Home, Focus, Controls, Devices, Activity,
  Moments, Appearance, then Kaboo token usage and Claude quota. The two data
  pages are fed over BLE from a companion Mac (`tools/usage_bridge.py`) through
  an always-on NimBLE GATT server that the app owns exactly once. All ten
  pages share one runtime, one screen, one
  header/footer, and one 200 ms master timer that now drives the 7 s tour, the
  1 s scene-step demos, Kaboo's 8 s card rotation, and BLE refresh together.
  The footer is visible on every page and names the left and right neighbour
  pages. Keys are modal: in browse mode UP and DOWN turn pages and OK runs the
  page's primary action (Kaboo's card advance, Player's Quick Actions); a long
  press on OK enters scene mode on pages with in-page interaction, where
  UP/DOWN move focus and OK acts, and another long press leaves it.
  The unattended tour skips the data pages and no longer applies an
  accessibility mode when it passes Appearance, so the theme no longer drifts
  each cycle; a manual OK on Appearance still applies one. The header keeps
  the battery percentage and adds a BLE link dot that refreshes every tick;
  the page counter was dropped from the title so the two fit. Home's dock and
  Player's status lines moved up to clear the persistent footer. Data-page
  labels are redrawn only when a new BLE packet, a card change, or a minute
  boundary makes their text differ, instead of on every tick. A digit-subset
  44 px face renders Kaboo's headline number.
- Added the first reusable AI Passport Glass System foundation: portable design
  tokens, four accessibility profiles, deterministic non-linear motion, one
  continuous three-button focus model, full-bleed content canvases, semantic
  solid groups, glass-control components, an adaptive display-quality runtime,
  and a public BSP performance snapshot. The default on-device review is now
  an eight-scene mobile-style
  interaction and motion reel covering Player, Home, Focus, Controls, Devices,
  Activity, Moments, and Appearance. UP is the invariant page key, while DOWN
  and OK operate the active scene; the automatic tour traverses every scene and
  yields on the first physical input. Toggle and Slider use glass thumbs with
  interruptible non-linear motion, Segmented and Focus indicators retarget from
  their sampled position, and the overlapping three-card deck remains a
  separate Motion Lab instead of defining the framework's default layout. List,
  settings, activity, and action scenes no longer repeat one large opaque
  rounded card; wallpaper-aware depth now comes from their canvas and controls.
  Capsule labels use optical baseline correction, segmented labels share the
  selection geometry, Dock labels are vertically centered, and Slider labels
  now sit above full-width tracks instead of colliding on one baseline. The unattended
  loop now starts on Player, holds each page for seven seconds, and exercises
  every scene's signature interaction at a readable one-second cadence,
  including all Activity states and Appearance application. Screenshot capture
  can record a timed sequence over one serial session, and diagnostic rendering
  always composites from the screen root so opaque canvases cannot hide later
  controls in captured PNGs.
- Added an `optimize-embedded-display` skill that teaches AI agents to measure
  MCU display pipelines, classify CPU, bus, DMA, invalidation, scheduling, and
  scanout limits, then select common or conditional optimization techniques for
  the actual animation. It includes a reusable technique catalog and a measured
  AI Passport ESP32-C3/LVGL case study without presenting Liquid Glass-specific
  constants or the experimental display overclock as universal defaults.
- Added a hardware-adapted Liquid Glass review screen with Regular, Clear, and
  High Contrast materials; three-ring precomputed edge optics; directional
  specular sweeps; stable window morphing; delayed content materialization; and
  button-triggered press, rim, and waveform feedback. The review now uses a
  source-tracked native RGB565 wallpaper and more transparent Regular/Clear
  fills so the material can be judged against real image detail. Pagination now
  uses a persistent card-deck transition: every card stays visible while moving
  between front, middle, and back ranks; the wrapping card follows one reversible
  depth curve and exchanges its Z plane only at the overlap midpoint. The
  rank-differentiated stack now uses a compact 10 px vertical rhythm and a
  restrained 4 px width step, keeping all three layers legible without making
  equivalent information cards look like different component sizes. Material and deck position are now independent: all three cards share one per-layer
  transmittance, natural alpha composition darkens overlaps, and rank changes
  only geometry, occlusion, content, and edge exposure. OK changes the material
  mode for the whole deck. A one-shot review tour demonstrates every state after
  boot and yields immediately to physical input; long OK still returns to the
  existing diagnostic menu.
- Added a low-memory `FAP_SCREENSHOT_V1` implementation and host capture tool so
  the 240x320 RGB565 display can be captured over USB Serial/JTAG without a
  full-screen RAM buffer.
- Replaced three overlapping full-card alpha fills with one allocation-free
  fused background renderer: exact RGB565 coverage-mask LUTs and a rounded-card
  event sweep write the wallpaper and glass directly into LVGL's active draw
  buffer, avoiding the decoded-image copy stage. Static three-ring rims,
  directional highlights, and moving glints are fused as short scanline spans
  instead of separate LVGL draw tasks. Card geometry now advances in one atomic
  deck animation; a bounded 8x8 tile planner coalesces fill, optical edge, and
  visible-content damage before invalidation, while content fades and pagination
  geometry redraw only their real pixels. Exhaustive host tests cover all RGB565
  colors, eight overlap states, fused edge/glint pixels, and no-miss dirty
  planning. On the connected board, the representative card-cycle render
  interval fell from about 87 ms to about 49 ms without changing the 640 ms
  motion trajectory. The demo temporarily shortens LVGL's refresh period to
  10 ms for denser motion sampling and restores the global 33 ms default on exit.
- Reduced Liquid Glass animation latency on the connected board by using an
  experimental 80 MHz LCD SPI clock, two 20-line DMA buffers, and the ESP-IDF
  performance compiler profile. Static full-screen tint and readability layers
  are now baked into the RGB565 wallpaper instead of alpha blended on every
  redraw. Added periodic display-pipeline metrics that separate CPU rendering,
  DMA wait, pixel traffic, and the theoretical SPI wire-time floor; this does
  not claim tear-free output because the board exposes no panel TE signal.
- Made mini-program BLE install compatibility a template-level invariant: fixed
  protected `cardid`/Recovery partitions, retained the five-second UP-key
  Recovery boot hook, and added CI validation for merged-image structure,
  partition MD5/ranges, the 3 MB app limit, and protected payload exclusion.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root by moving GitHub-recognized community
  documents into `.github/`, moving the changelog into `docs/`, and updating
  every reference. Root-level Markdown remains allowed when it is the natural
  project-level location.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.

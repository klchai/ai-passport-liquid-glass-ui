<p align="right">
  <a href="DESIGN.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Glass System

## Scope and maturity

This document defines the durable visual and interaction boundary of the
hardware-native Glass System. The current foundation implements tokens, four
accessibility profiles, deterministic motion, three-button focus, core content
and control components, adaptive display quality, an eight-scene showcase, and a
separate legacy Motion Lab. Device cost fields remain pending until the current
build is flashed and measured on the physical board.

## Direction contract

- **Thesis:** one optically active control plane floats above stable content.
- **Visual world:** a deep photographic background, full-bleed quiet content
  canvases, restrained cool light, and glass reserved for action and focus.
- **First view:** the content layer explains itself immediately; the persistent
  bottom platter communicates input and page position without covering content.
- **Signature interaction:** one trigger expands into its own menu with a
  lightly under-damped 560 ms morph, then returns to the same physical origin.
- **Risk:** RGB565, low refresh rate, and missing TE can make subtle motion
  disappear. Geometry and state must therefore remain legible at every sampled
  frame and under Reduced Motion.

## Layer model

```text
wallpaper / product content
        ↓
edge-to-edge quieting canvas or semantic solid group
        ↓
one glass control plane
        ↓
transient menu, focus, or feedback from its physical origin
```

Ordinary list and settings content flows directly on the canvas instead of
sitting inside a repeated page-sized card. Compact solid groups remain valid
when dense controls need a stable reading surface. Multiple glass objects are
permitted only when the product meaning requires them, such as control groups.
Overlapping equal-purpose glass cards are not a default layout.

## Foundations

- Spacing: 4, 8, 12, and 16 px.
- Radii: 14 px controls, 18 px semantic content groups, 22 px floating platters.
- Type: Montserrat 20 for page-level hierarchy and Montserrat 14 for controls,
  metadata, and measurement. The firmware does not claim CJK support.
- Semantic color: text, muted text, accent, positive, warning, danger, stable
  content surface, and neutral control tint.
- Hardware safe area: 16 px horizontal content inset, header above y=44, scene
  below it, and persistent control platter at y=272.

The portable source is `design/liquid-glass/tokens.json`; the firmware source is
`main/ui_glass_theme.*`.

## Material semantics

| Material | Use | Do not use |
| --- | --- | --- |
| Regular | persistent interactive platter and normal focus | every content card |
| Clear | small focus lens or explicit optical comparison | text-heavy content |
| Contrast | high-contrast and reduced-transparency controls | decorative depth |
| Chrome | quiet status chrome with no glint | primary content container |

The material engine uses RGB565-friendly precomputed opacity, edge, specular,
and glint profiles. It does not perform a blur kernel. Background borrowing is
approximated at the optical rim; the fused compositor handles the specialized
three-card study without making that layout a framework default.

## Motion system

| Token | Duration | Meaning |
| --- | ---: | --- |
| Press | 120 ms | immediate physical compression, followed by release |
| Focus | 320 ms | continuous movement of one shared focus lens |
| Materialize | 520 ms | material and content becoming available |
| Morph | 560 ms | trigger becoming menu, popover, or sheet |
| Page | 400 ms | whole-scene continuity at the panel's visible cadence |

`ui_glass_motion.*` provides integer cubic easing, a deterministic spring with
one restrained overshoot, and reusable morph geometry. Reduced Motion keeps an
80 ms press response and makes spatial transitions immediate.

## Input and focus

- A short UP press advances to the next showcase page; double UP returns to the
  previous page.
- DOWN performs the current scene's secondary action, normally advancing the
  shared Focus Model.
- OK activates the focused control or performs the scene's primary action.
- Long OK remains the repository-wide return-to-menu action.
- Auto tour starts on the initial Player scene, demonstrates every scene's
  primary motion and state feedback, and stops on the first physical input.

Focus is an object with a continuous trajectory, not a per-row border that
appears and disappears. The focused state remains visible even when motion is
disabled.

## Components and states

Every component supports the relevant subset of default, focused, pressed,
selected, disabled, loading, and error. Accessibility profiles are variants of
the same component, not detached copies.

The initial reusable C API includes content panel, glass platter, focus lens,
row, toggle, and slider. Morph menu, dock, and device status are proven in the
showcase and must be extracted before being advertised as reusable APIs. The
complete honest inventory is `design/liquid-glass/components.json`.

Every public component must eventually publish RAM, Flash, typical and worst
dirty pixels, and typical and worst frame time. Unknown device values stay
`pending-device`; estimates are not measurements.

## Patterns and showcase

The on-device showcase is an eight-scene interaction and motion reel. It keeps
the original component coverage but presents it through credible mobile-style
contexts instead of implementation categories. The page order is deliberately
different from the stable internal component identifiers:

1. Player: edge-to-edge media content, playback state, and a source-origin
   Quick Actions morph
2. Home: floating Dock navigation with continuous selection and content motion
3. Focus: segmented mode selection, a toggle, choices, and one moving Focus Lens
4. Controls: an animated glass slider thumb, stepper, and progress adjustment
5. Devices: list traversal, persistent focus, and row activation feedback
6. Activity: one coherent offline-sync flow across progress, paused, and error
   states
7. Moments: a photographic content task with primary, glass, and destructive
   actions
8. Appearance: Standard, High Contrast, Reduced Transparency, and Reduced Motion
   as selectable system profiles

UP is the invariant page key. DOWN and OK remain available to the current scene,
so the device demonstrates two meaningful interactions without changing the
navigation grammar on every page. Rapid focus, toggle, slider, and segmented
input retargets the existing visual object from its sampled position rather than
starting competing animations. The unattended loop covers Player menu morph and
selection, all Dock destinations, segmented/toggle/focus motion, all three
adjustment types, multiple list rows, all Activity states, all Moments actions,
and application of the next Appearance profile.

The old three-card deck remains available as Motion Lab. It demonstrates
continuous depth exchange and compositor optimization, but it is not the
default product pattern.

## Runtime and performance

The runtime owns the wallpaper compositor, semantic mode, refresh timer, and a
quality controller with hysteresis:

| Quality | Refresh period | Animated glass limit | Glint |
| --- | ---: | ---: | --- |
| Full | 10 ms | 6 | enabled |
| Balanced | 16 ms | 3 | enabled |
| Economy | 33 ms | 1 | disabled |

Three consecutive slow samples may reduce quality; six fast samples are needed
to restore it. One full-screen transition cannot permanently downgrade the UI.
The BSP publishes one-second snapshots for update rate, CPU render time, DMA
wait, SPI wire-time floor, updated pixels, invalidation requests, and DMA heap.

## Prohibited defaults

- Glass as a generic content-card style.
- Nested or overlapping glass without product meaning.
- Menus that appear away from their trigger.
- Motion that creates or destroys an object instead of preserving its path.
- Linear motion used for primary focus, morph, or page transitions.
- Apple assets, SF Symbols, UI kits, or templates embedded in the repository.
- Performance numbers without a reproducible physical-device capture.

## Figma boundary

The Figma library mirrors Foundations, Materials, Controls, Navigation,
Presentation, Device Patterns, Accessibility, and Performance Annotations.
Tokens and the component inventory live under `design/liquid-glass/`. This is an
original library for the AI Passport; Apple resources are reference material,
not redistributable source assets.

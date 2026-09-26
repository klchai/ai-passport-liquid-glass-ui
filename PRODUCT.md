<p align="right">
  <a href="PRODUCT.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Product

<!-- impeccable:product-schema 1 -->

## Platform

Embedded firmware for the ESP32-C3 AI Passport.

## Users

- AI Passport owners who install and evaluate community firmware on the real
  device.
- Open-source firmware developers who need reusable, hardware-native UI
  primitives instead of copying a showcase screen.
- Designers who need a shared vocabulary between design tools and the RGB565
  runtime.

## Product Purpose

Provide an original, open UI foundation for building refined glass-inspired
interfaces on the 240 × 320 AI Passport. Success means one system can carry a
design specification through tokens, motion, components, runtime rendering,
product patterns, an on-device showcase, and measurable performance tests.

## Positioning

This is a hardware-specific design system, component library, rendering
runtime, and benchmark suite in one package. Every visual primitive exposes its
interaction states and its display cost, so developers can adapt the system to
other animations without inheriting demo-specific layout code.

## Operating Context

The primary review loop is design specification → firmware build → segmented
USB flash → physical button interaction → serial performance telemetry and
screen capture. The device is operated with UP, DOWN, and OK rather than touch.

## Capabilities and Constraints

- ESP32-C3, 8 MB Flash, no PSRAM, ESP-IDF 5.5.3.
- ST7789P3 240 × 320 RGB565 display over SPI, with no readable framebuffer or
  exposed tearing-effect signal.
- Internal RAM is shared by LVGL, display DMA, audio, networking, and tasks.
- The UI must preserve the protected Recovery and mini-program BLE installation
  contracts defined by the repository.
- Reusable board behavior belongs in `components/bsp`; UI state, motion,
  components, patterns, and demos belong in `main`.

## Brand Commitments

- The visual language is inspired by the interaction and material principles
  of modern Apple interfaces, but it is original and hardware-native.
- Content remains on an opaque base layer. Glass is reserved for navigation,
  controls, focus, menus, transient feedback, and explicit material labs.
- Menus and popovers morph from their trigger; they do not appear as unrelated
  floating rectangles.
- Apple UI kits, SF Symbols, templates, and other restricted Apple resources
  are not embedded or redistributed.

## Evidence on Hand

- Optical material profiles: `main/ui_glass_optics.*`.
- RGB565 fused compositor and dirty-region planner:
  `main/ui_glass_compositor.*` and `main/liquid_glass_compositor_core.*`.
- Continuous motion study and showcase scenes: `main/liquid_glass_motion.*` and
  `main/showcase_scenes.c`.
- Device screenshot capture and display telemetry are implemented in the
  repository. No production performance claim should be invented without a
  captured device sample.

## Product Principles

1. Content first; glass communicates interaction and hierarchy.
2. One coherent control plane is better than stacked translucent decoration.
3. Motion preserves object identity and spatial origin.
4. Performance cost is part of every component's public contract.
5. Accessibility modes alter material and motion semantics, not only colors.

## Accessibility & Inclusion

The system must provide high contrast, reduced transparency, and reduced motion
profiles. Focus must remain visible under three-button navigation, and all
essential states must remain understandable without relying on animation alone.

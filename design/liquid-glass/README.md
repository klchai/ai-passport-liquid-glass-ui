<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport Glass Design Package

This directory is the design-tool boundary for the original, hardware-native
glass system. It does not contain Apple UI kits, SF Symbols, screenshots, or
redistributable Apple templates.

## Files

- `tokens.json` uses the Design Tokens Community Group format. Import it through
  a compatible token workflow or translate it into native Figma Variables.
- `components.json` is the implementation inventory. `implemented` means a
  reusable public C API exists; `showcase` means the behavior is demonstrated
  but still needs extraction; `planned` is not a shipping claim.

The C source of truth is `main/ui_glass_theme.*` for semantic tokens and
`main/ui_glass_widgets.*` for implemented components. Keep code and design
exports aligned in the same change.

## Figma library structure

Create four variable collections from `tokens.json`: `Color`, `Spacing`,
`Radius`, and `Motion`. Components should use variants for state and
accessibility mode rather than detached copies. The library page order is:

1. Foundations
2. Materials
3. Controls
4. Navigation
5. Presentation
6. Device patterns
7. Accessibility
8. Performance annotations

Each component page records anatomy, correct use, prohibited use, every input
state, motion origin, and a link to the matching entry in `components.json`.
Device measurements remain `pending-device` until captured from the physical
board.

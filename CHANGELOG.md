# Changes

## 0.3.0-preview.2

- Added configurable concealment tags for regular invisibility, invisibility to undead, both, and unknown concealment. Choose tags, parentheses with tags, a name suffix, or off.
- Read known concealment from Hailstone's nameplate context and exact NPC invisibility types from compatible servers.
- Give the current target and nearby plates priority in crowded scenes, with time limits on plate preparation and stacking. Reuse text measurements within a frame and consider colors across NPCs of the same level.

## 0.3.0-preview.1

- Made Crest a standalone optional add-on using the generic nameplate canvas service from the shared UI API update.
- Moved all painting, bars, skins, icons, layout and stacking into Crest’s own Lua renderer.
- Removed client-bundle packaging; the runtime archive contains only the add-on, documentation and fourteen profiles.
- Added frame/owner cleanup, current-token submission, incarnation-based cache cleanup and consistent animation/cache clocks.
- Added independent renderer and generic adapter regression coverage and a standalone preview tool.

## 0.2.0-preview.1

- Renamed the add-on to Hailstone Crest, with automatic managed activation and an Enabled control in Add-ons.
- Added visible color swatches with a hue wheel, RGB/hex inputs and quick colors.
- Added complete Bare, Inset, Forged, Glass, Runic, Split and Tactical designs alongside the standard and role profiles.
- Added explicit through-wall display and default-enabled manual plate selection, with drag, UI, cursor, identity and occlusion guards.
- Kept compatibility indicators and private packs automatic and hidden from the visible profile list.

## 0.1.0-preview.1

- Initial configurable world HP/resource/cast replacement using Hailstone nameplate presentation API 1.
- Usable defaults, ten profiles, 185 settings, unit overrides, rules, search, personal resource window and data-only import/export.
- Passive awareness, pacify, mez and shared-mark support with bounded validation and stale-status expiry.
- Host-controlled visibility, occlusion, overlap separation and ordinary-caption fallback.

# Changes

## 0.3.0-preview.10

- Refresh overhead level labels when the server announces a new level, including for nearby players who stay in the zone. On clients with stale spawn levels, this uses Crest's existing packet permission.

## 0.3.0-preview.9

- Hide blank NPC nameplates, including the decorative bodies in the Plane of Tactics arena. Named NPCs and real corpses still follow your Crest settings.

## 0.3.0-preview.8

- Player plates follow Hailstone's `/show` modes for surnames, titles, and suffixes. Guild tags appear when the client supplies its guild caption to Crest.
- Added server-supplied NPC descriptions beneath NPC names. Pet owner names follow the client setting when Hailstone supplies the caption.

## 0.3.0-preview.7

- Added native Linux x64 and Apple Silicon macOS packages alongside Windows x64.
- Restored the full configuration window, themes, saved profiles, ordered name rules and personal resource window in the DLL edition.
- Restored native plate layouts, per-unit styling, visibility and click controls, cast customization, known auras, optional server indicators, and scoped group and extended-target resource observations.
- Restored consider colors for name and level, below-bar level placement, and target chevrons that clear wide plates.
- Added character saves and import of saved Lua Crest settings and profiles.

## 0.3.0-preview.6

- Open native settings and show status in chat with `/crest`. Commands report the change in chat.
- Added a Crest menu shortcut and an enable toggle in the stock Plugins options.

## 0.3.0-preview.5

- Published the Windows x64 DLL as the default Crest package. It requires a Hailstone build with the merged native canvas SDK.
- Added `/crest` controls for the native renderer and concealment modes. Switching from the Lua package requires a manual reinstall through Add-ons.

## 0.3.0-preview.4

- Added an option to color names and levels by consider color.
- Added level placement above or directly below the health bar.
- Fixed target chevrons overlapping other parts of wide nameplates.

## 0.3.0-preview.3

- Added **Parentheses only** for the classic concealment cue without a type tag.

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

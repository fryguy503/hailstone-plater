# Hailstone Crest

Crest is a standalone optional nameplate add-on for Hailstone. Its archive contains its own Lua renderer, visual themes, settings, rules, indicators and profiles. The normal Add-ons system manages installation and automatic activation. Open settings from the in-game **Crest** menu or `/crest`.

This preview requires Hailstone’s generic nameplate canvas API 1 and managed Lua add-on support, being integrated into the shared UI API update. The add-on works independently of the selected interface. Its package contains no client executable, client patch, game assets or interface replacement. Hosts without the service keep ordinary captions and show a compatibility notice.

The preview archive is local and unsigned; a public add-on release has not been published. Release installation uses **Add-ons**, with an **Enabled** switch. Lua is the implementation language; users do not need to run a loose script.

## Complete visual themes

The included visual designs change frames, fills, label placement, target decorations, indicators and cast layout together:

| Theme | Appearance |
|---|---|
| Default | Balanced solid frame, names above, resources and casts grouped together |
| Bare | Thin unframed strips, names below, side indicators, detached cast bar |
| Inset | Square boxed bars, inset names, health on the right, target chevrons |
| Forged | Raised metal bevels, glossy fills, side indicators, target brackets |
| Glass | Rounded glass frames, inset names, lower indicators, detached casts |
| Runic | Angular ornamental frame, segmented fills, side sigils and chevrons |
| Split | Lightweight health strip, narrower separate cast bar and side indicators |
| Tactical | Segmented dashboard bars, inset labels, lower indicators and brackets |

Tank, Healer, Large, Cast Focus, Raid and Classic Names provide six additional role-oriented setups. The fourteen examples are also included as `.crest` profile files.

## Controls

- **Click plates to target** starts enabled. A real left click selects the displayed unit; dragging, open UI windows, cursor items and invalid/deleted units keep their normal input protections. Right clicks do not attack or activate the unit through Crest.
- **Show plates through walls** starts disabled. When enabled, it permits displaying and manually selecting otherwise occluded plates. Invisible/hidden actors, host name switches, distance/frustum limits and the server's spell/action rules still apply.
- Every color has a visible swatch. Click it for a hue wheel, RGB inputs, hexadecimal entry and quick colors. Changes are visible immediately.
- The 223 settings include per-unit appearances, combat/rest/target styling, health/mana/endurance, cast timing, known auras, numeric/shared marks, low-health and aggro emphasis, bounded stacking, a personal resource window, literal name rules and profile import/export.

Settings are kept separately per server/world/character. Up to sixteen named profiles and sixty-four ordered literal name rules are supported. Profile imports are bounded data, not executable scripts.

## Managed add-on behavior

Crest is an installed add-on, with an **Enabled** switch in **Add-ons > My add-ons**. Its renderer, bars, icons, layout, stacking, UI and rules all ship in Crest’s Lua sandbox. The shared host API supplies filtered world anchors, clipping/depth and guarded physical input. Users do not need to start a loose script or enter `/lua run`.

Disabling/removing the managed add-on removes its presentation hooks and hit areas. Updating it preserves the activation choice. Capability ceilings and server policy still apply. Release publication is separate from source development and local packaging.

`/crest` opens settings. `/crest on`, `off`, `toggle`, `save`, `status` and `help` are optional shortcuts. Turning replacement off restores ordinary captions.

## Supplied data and safety

Remote HP is normally a percentage. Actual HP is shown only for yourself. Resource bars use the host's known self/group values, supplemented by exact received updates for current group/extended-target actors. Unknown values remain hidden; a legitimate zero remains visible. Supplemental resource observations clear when tracking scope, entity identity or actor lifetime ends.

Optional indicators activate automatically when compatible data arrives. These may include detection of your current concealment, pacify, mez and shared marker symbols. Detection does not claim a permanent see-invisible stat. Incomplete status snapshots never replace a complete one, and status expires after five seconds without a complete refresh. Shared marks use authoritative received updates, with group and raid scopes kept separate.

All packet handlers observe only. Crest sends no custom packets, does not automate combat, and writes no game assets. Manual targeting uses the normal host target path. Cast bars do not claim interruptibility, and aura badges show only known self/current-target buffs. Missing local spell art uses a vector fallback.

The declared capabilities are `read,commands,ui,extend,packets`. `packets` is optional at runtime: if denied, host-known bars still work while supplemental hints/pools remain unavailable. No action, movement, automatic-targeting, chat-send, network or login capability is requested.

## Authoring and validation

Build the add-on with Hailstone’s official packager:

```powershell
python tools/package.py --client-source <compatible-hailstone-source> --lua lua
```

The package is a managed Lua add-on (`autoload 1`) using the reusable `hailstone.nameplates` service. It submits generic drawing primitives; the host contains no Crest bar, skin, preset or stacking recipes. Output goes into `release/0.3.0-preview.1/` so earlier previews remain separate. `tools/verify_package.py` verifies the add-on archive and source bytes without a client bundle.

The optional `tools/preview.cpp` evaluates the actual shipped presets through Crest’s engine and renderer with Lua and Dear ImGui, then writes an offline comparison image. It uses no private client headers or client engine library.

Run the Lua checks with `tools/test.ps1 -Lua <lua.exe>`. See [validation details](docs/VALIDATION.md) for exact checks, external-fixture limitations, and live acceptance still pending. No push or release publication is performed by these tools.

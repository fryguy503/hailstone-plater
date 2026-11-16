# Crest validation — 2026-11-15

Crest 0.3.0-preview.1 is a standalone Lua add-on preview. Its renderer, bars, themes, icons, layout and stacking ship in the add-on. The client supplies a reusable world-nameplate canvas service through the shared UI API update. No client executable or client patch is included in the add-on archive. Release publication, deployment, live gameplay and physical-controller acceptance remain pending.

## Add-on checks

All seven Lua 5.4 suites pass: **131 cases and 162,926 assertions**. They cover configuration/import bounds, fourteen complete profiles, color swatches and popup flows, scoped resources, optional indicators, renderer primitives and metrics, deterministic bounded stacking, lifecycle cleanup, denied optional packet access, compatibility fallback and balanced UI calls. Regression cases cover resource observations across actor reincarnation and clocks with different epochs.

The default profile enables physical plate clicks and disables through-wall display. Tests verify that both choices reach the generic canvas models. Unknown resources stay hidden; a known zero remains visible. Compatibility packs are neither listed nor exported. Public files contain no server-specific branding.

## Shared host API checks

The generic API is being integrated into the existing UI API draft; it is independent of Crest and includes no Crest skins, bars, preset recipes or stacking implementation. Its focused native harness passes **26 cases and 782 assertions** on Windows/MSVC and Linux/GCC 13. The Linux run enables ASan, leak detection and UBSan with no reported errors or warnings. The harness links the actual registry, Lua binding, primitive painter and guarded input implementation with Lua and Dear ImGui.

The checks cover stale frame tokens, owner lifetime, limits, primitive validation, font ink/advance measurements, vertex budgets, nearest-hit geometry and click gesture identity. Changed translation units pass MSVC syntax checks. API/package Python checks, generated API documentation, migration mock, core-name, wiki-sync and diff checks pass. Full shared-host Windows/Linux builds and suites are being run by the UI API integration work; these focused results do not establish those full-suite results.

The shared host also provides managed Lua activation and color widgets. Previously recorded full-client results for the superseded 0.2 companion-client implementation do not validate this refactor.

## Package checks

The official add-on packager creates a managed script add-on with `autoload 1`. The standalone verifier checks manifest fields, SHA-256, archive CRCs, safe paths, duplicate entries and exact agreement with the current source. It checks the fourteen exported examples and rejects client binaries, patches, game archives and preview client directories. The renderer is part of the runtime archive. The optional preview executable and its build dependencies are development tools and are not packaged.

## Visual and live scope

The comparison image evaluates the actual shipped presets through Crest's Lua engine and renderer. It uses the same synthetic unit data and colors for eight structural themes and preserves each preset's sizes and visibility. A development-only Dear ImGui/software painter renders the generic commands with real font advance and ink measurements. It uses no private client headers or engine library. The resulting PNG has been visually inspected. It demonstrates offline layout; it is not an in-game screenshot or a Vulkan rendering test.

The host accepts only physical clicks and validates the current owner, connection, zone and actor incarnation. Open UI, cursor payloads, drag gestures and focus loss cancel or block selection. Through-wall display is an explicit off-by-default choice; normal server action and range rules still apply. Host visibility, name switches, frustum and world range gates remain authoritative. Crest does not fabricate unavailable resource or aura data.

Actual Vulkan rendering, live server traffic, sustained zoning, multiplayer behavior, every preference combination and physical controllers remain unverified. The archive is local and unsigned. Source pushes, release publication and live acceptance are separate steps. No server change is required or performed.

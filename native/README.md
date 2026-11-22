# Compiled Crest for Hailstone

Crest draws configurable world nameplates with themes, per-unit styling, health and resource bars, observed casts, known auras, and optional server indicators. Hailstone's native nameplate canvas handles depth, actor visibility, and manual clicks.

## Install or switch from Lua Crest

1. Start Hailstone with the merged native nameplate SDK on Windows x64, Linux x64, or Apple Silicon macOS. The published `0.7.1-beta` client predates this API. Compatible source builds can report the same version number.
2. At the login screen, open **Add-ons → My add-ons** and remove the Lua `crest` entry if installed. Hailstone requires this one-time step to switch from a script to a compiled plugin. If you used the earlier `crest-native` preview, unload it so Crest has the nameplate canvas to itself.
3. In **Add-ons**, paste the Crest repository link or the release link and install `crest`. Hailstone selects the package for your platform and installs `crest.dll`, `crest.so`, or `crest.dylib`. Use Hailstone's plugin controls if the server requires consent or manual loading.

Type `/crest` in game to open the full configuration window in Modern or Gamepad. It includes **Settings**, **Unit types**, **Name rules**, **Profiles**, and **Preview & status**. The Plugins menu has a **Crest** shortcut, and **Options → Plugins** has an **Enable Crest nameplates** toggle.

Changes save per character. On first use for a character, Crest imports recognized saved settings and named profiles from the matching Lua Crest file if one exists. The existing `CREST1` text format works for profile import and export.

Use Hailstone's `/shownames` (or `/show`) to change player captions immediately. Modes 2, 3, 4, and 6 show surnames. Modes 4 through 6 show titles and suffixes. Modes 3 and 4 show guild tags when the client provides the guild caption to add-ons. Hailstone 0.7.2-beta supplies the player name and titles, and guild tags require a newer client with guild caption support. `/show off` hides player plates. The Crest short-names setting applies to NPCs and corpses.

Server-supplied NPC descriptions such as `(Shadow Knight Guildmaster)` appear below NPC names, including on Hailstone 0.7.2-beta. Pet owner lines follow Hailstone's Pet Owner Names option on clients that supply the secondary caption.

Use `/crest help` to list commands. `/crest parens` shows parentheses only, `/crest parens-tags` shows parentheses and type tags, `/crest tags` shows tags, `/crest suffix` adds a name suffix, and `/crest concealment-off` hides the cue. Use `/crest on`, `off`, `toggle`, `save`, or `status` for common actions. `/crest stop` suspends the renderer until you run `/crest on` or reload the add-on.

## Build and package

Run from the Crest checkout with CMake 3.25 or later, a C++20 toolchain, and Python 3.8 or later:

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release -DHAILSTONE_SDK_DIR=<path-to-hailstone-sdk>
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
```

Build on Windows x64, Linux x64 (Ubuntu 24.04 baseline), and Apple Silicon macOS. The macOS library targets macOS 13.3 or later. Sign it ad hoc and verify it before packaging.

```sh
codesign --force --sign - --timestamp=none path/to/crest.dylib
codesign --verify --strict path/to/crest.dylib
```

The **Crest native builds** workflow compiles, tests, and checks that each library loads on its target OS. To assemble its three artifacts into a single Hailstone manifest and the platform ZIPs, run:

```sh
python native/tools/package.py --client-source <path-to-hailstone-source> \
  --platform windows-x64=path/to/crest.dll \
  --platform linux-x64=path/to/crest.so \
  --platform macos-arm64=path/to/crest.dylib \
  --out native/release/0.3.0-preview.9
```

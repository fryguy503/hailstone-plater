# Crest Native preview

Crest Native is an experimental Windows x64 compiled add-on. It draws names, known HP and mana, observed casts, and the five concealment modes through Hailstone's shared nameplate canvas. The canvas uses Hailstone's depth, visibility, and manual click handling. The add-on uses `read`, `ui`, and `extend` for the canvas, `commands` for preview controls, and optional `packets` for exact NPC concealment from a compatible server.

## Install the preview

Use a Windows x64 Hailstone build with the merged native nameplate SDK. This preview was built against upstream revision `8cbd0520`, which includes the candidate and command strides. The downloadable `0.7.1-beta` predates those SDK fields, even though a source build can report the same version number.

Paste the tagged native release URL into Hailstone's **Add-ons** manager. It installs as `crest-native`. Test in a separate profile with a single nameplate provider active. Servers that require the shared add-on repository can apply that requirement to both packages and may request native code consent. Use a test profile and server where you can choose which provider to run.

Load `crest-native` using the client's normal plugin controls. `/crestnative parens`, `parens-tags`, `tags`, `suffix`, and `concealment-off` switch the cue for the current session. `/crestnative off` and `/crestnative on` turn the native provider off and on.

The preview has basic bar rendering and command-based settings. The settings window, saved profile migration, themes, aura rendering, and exact group and extended-target resource updates remain in development.

## Build and package

Run from the Crest checkout with a C++20 toolchain:

```powershell
cmake -S native -B native/build -DHAILSTONE_SDK_DIR=<path-to-hailstone-sdk>
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
python native/tools/package.py --client-source <path-to-hailstone-source> --build-dir native/build --out native/release/0.1.0-preview.1
```

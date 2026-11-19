# Crest for Windows x64

Crest draws world nameplates with names, known health and mana, observed casts, and five concealment display modes. Hailstone's native nameplate canvas handles depth, actor visibility, and manual clicks. Compatible servers can supply exact NPC concealment types through the optional packet permission.

## Install or switch from Lua Crest

1. Start a Windows x64 Hailstone build with the merged native nameplate SDK. The downloadable `0.7.1-beta` predates this API. Compatible source builds can report the same version number.
2. At the login screen, open **Add-ons → My add-ons** and remove the Lua `crest` entry if installed. Hailstone requires this one-time step to switch from a script to a compiled plugin. If you used the earlier `crest-native` preview, unload it so Crest has the nameplate canvas to itself.
3. In **Add-ons**, paste the Crest repository link or the release link and install `crest`. This installs `crest.dll`. Use Hailstone's normal plugin controls if the server requires consent or manual loading.

Type `/crest` in game to open the native settings window in Modern or Gamepad and get status in chat. The Plugins menu has a **Crest** shortcut, and **Options → Plugins** has an **Enable Crest nameplates** toggle.

Use `/crest help` to list commands. `/crest parens` shows parentheses only, `/crest parens-tags` shows parentheses and type tags, `/crest tags` shows tags, `/crest suffix` adds a name suffix, and `/crest concealment-off` hides the cue. `/crest off` and `/crest on` disable or enable Crest nameplates. Changes apply to the current session.

The native settings window controls the renderer and concealment display. Saved profiles, themes, name and level styling, auras, and supplemental group and extended-target resource readings are available in the Lua `v0.3.0-preview.4` release. To roll back, use a Hailstone build that runs Lua Preview 4, remove the native `crest` entry in **My add-ons**, and install from the Lua release tag. The merged SDK test build rejects Preview 4 during Lua compilation. Keep your working Lua installation on other platforms or Hailstone builds without the native SDK.

## Build and package

Run from the Crest checkout with a C++20 toolchain:

```powershell
cmake -S native -B native/build -DHAILSTONE_SDK_DIR=<path-to-hailstone-sdk>
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
python native/tools/package.py --client-source <path-to-hailstone-source> --build-dir native/build --out native/release/0.3.0-preview.6
```

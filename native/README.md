# Crest native development build

This branch builds an experimental `crest-native` library against Hailstone's public native nameplate canvas SDK. The library uses the same depth, visibility and manual-click path as the Lua add-on. It draws a basic name, known HP and mana, observed casting progress, and the five concealment modes. It uses `read`, `ui`, and `extend` for the canvas, `commands` for the test controls, and optional `packets` for exact NPC concealment from a compatible server.

Run from the Crest checkout with a C++20 toolchain:

```powershell
cmake -S native -B native/build -DHAILSTONE_SDK_DIR=<path-to-hailstone-sdk>
cmake --build native/build --config Release
ctest --test-dir native/build -C Release --output-on-failure
cmake --install native/build --config Release --prefix <test-profile-plugins-dir>
```

Use the SDK branch's Hailstone executable and an isolated test profile. Disable the Lua Crest add-on in that profile so Crest Native can claim the same nameplates. Load `crest-native` using the client's normal plugin controls. `/crestnative parens`, `parens-tags`, `tags`, `suffix`, and `concealment-off` switch the cue for this run. `/crestnative off` and `/crestnative on` control the test provider.

The Lua add-on remains the published package. Native port work still includes the settings UI, saved profile migration, theme and aura rendering, and wiring the exact resource decoder into the group and extended-target lifecycle. The managed add-on installer preserves a package's kind, so a native release requires a migration plan for current script installs.

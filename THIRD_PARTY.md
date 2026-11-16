# References

Crest uses Hailstone's documented Lua add-on APIs and generic world-nameplate canvas service. Its independently written MIT-licensed Lua owns all visual recipes, layout and stacking. No host implementation is copied into the runtime package.

Optional indicator/resource wire layouts were checked against the user's Citadel protocol definitions and server-side nameplate awareness/marker contracts. Those sources were protocol references. DLL hooks, MacroQuest/D3D rendering implementations, icon art and game assets were not copied. Generic indicators are drawn independently; spell art is read from the player's own files by the host.

The supplied screenshots and [WoW reference add-on](https://github.com/Tercioo/Plater-Nameplates) inspired the range of visual layouts. Source code, fonts, textures, executable mods and profile data were not imported. Crest uses its own CREST1 data format.

The optional offline development preview tool links Lua 5.4 (MIT), Dear ImGui (MIT) and stb_image_write (public domain/MIT alternative). It uses an explicitly selected local font; that font is not redistributed. These build dependencies and the preview executable are not included in the add-on archive. Their upstream license notices remain with their respective source distributions.

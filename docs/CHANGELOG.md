# Changelog

## 1.0.0 (October 2026)

Tomb Raider: Legend in VR on the Valve Steam Frame. LegendFrame is a fork of RototRobot's Tomb-Raider-Legend-VR-Port 1.1.0. All of the VR work is theirs: the stereo view, head tracking, motion controls, the third-person VR camera, first-person mode and god mode. You need your own copy of the game from Steam.

Tomb Raider: Legend is a 32-bit Windows game. On the Frame it runs through Proton and an x86 emulator, and every draw call costs a lot. The original mod draws the whole scene twice, once per eye, which made it CPU-bound at about 40 frames a second on the Frame, whatever the resolution. LegendFrame makes the game cheap enough to run at the headset's refresh rate.

### Performance

- The original mod ran at about 40 frames a second in gameplay on the Frame. LegendFrame holds the headset's 72 Hz most of the time, with dips to the mid 60s.
- Each frame takes about 14 ms instead of about 25.
- Lowering the resolution never helped the original mod. The time was going into the doubled draws, not the GPU.

### Rendering

- **Single-pass stereo.** Each draw from the game is sent once and lands in both eyes. The original mod sends it twice. The images are the same. It is done in the bundled DXVK build by viewport index: one draw with twice the instances, each instance routed to one eye's half of the image. A draw it cannot take (instanced, fixed-function, per-eye textures, the reflection pass) falls back to the two-pass path on its own.
- **Cheaper eye maths.** Both eyes' matrices come from one calculation, about 47 percent fewer instructions per stereo draw, and the mod is built with SSE2 maths, which the emulator runs faster.
- **No idle wait before each frame goes to SteamVR.** The CPU no longer sits waiting for the GPU before handing over a frame.
- The original two-eye renderer is still there as a setting (`stereo_mode=twopass`) and a live toggle in the settings panel (or Shift+F6).

### Fixes

- **God mode carries stay whole.** In the original mod, picking up a character after a cutscene left parts of her stretched back to the pick-up spot. Lara's wet and dirty overlay had the same problem, the belt, boots and shirt smearing while she was carried, because it is blended from her pose before the carry moves it. Both are fixed (`god_carry_fix`, on by default).
- **Single-pass shader constants.** In single-pass, a shader that sets its own values in the first four constant registers could have them replaced by the per-eye ones when it read them by relative index. It now keeps its own, so single-pass draws match two-pass.

### Steam Frame controls

The upstream layout was made for Oculus Touch. On the Frame, X and Y did the same as B, three d-pad buttons threw grenades, the right Menu button did nothing and the prompts named buttons the Frame doesn't have. Every button now has its own job:

- **A, B, X and Y** are on the right hand: A jumps, B crouches, rolls and dives, X is precision aim and Y throws grenades and flares.
- **The d-pad is the game's gear cross** in every mode: up medipack, right binoculars, down switch weapons, left personal light. It flashes the item wheel when I press it and moves through menus. The left stick's radial gear menu still works.
- **First-person weapon select.** D-pad down is the game's weapon select in first person as well, and the holsters keep up with the weapon it picks. With only the pistols, the game shows its weapon selector.
- **Right Menu pauses**, and holding it opens the settings panel. **View opens the PDA.**
- **Quick draw.** In immersive first person with the guns holstered, the fire trigger or X draws the pistols straight away. The pull that draws doesn't fire. `frame_quick_draw` turns it off.
- **Right bumper holsters.** Holding it for a moment puts away whatever is drawn: the pistols to the hips, the long gun to the shoulder. The Frame's cameras often lose the controllers at the hips and behind the shoulder, so a button is the dependable way, but the reaches still work. `frame_holster_hold` sets how long.
- **Left bumper held** does the board-mode finger flick.
- **Binoculars, then aim.** In the binocular view in third person, X puts the binoculars away and then aims.
- **Precision aim feedback.** The game only lets Lara aim precisely on her feet. If I press X while she jumps, hangs or climbs, a short note in the headset says why, instead of nothing happening.
- **Prompts name the Frame's buttons** (A, B, X, Y, D-pad Up and so on, Menu, View). English only; other languages keep upstream's names.

If SteamVR kept an older binding, select the supplied one again under Manage Controller Bindings.

### HUD

- **HUD position.** Sliders move the whole HUD, and it starts nudged up and to the left.
- **HUD layout.** The health bar, the ammo readout and the item wheel each move as one piece, with a left / right and a down / up slider for each in the settings panel. Two more move the whole HUD, and a Reset to defaults button puts everything back.
- **The HUD stays in view while the panel is open,** so I can watch the sliders move it.
- On the Frame the health bar and the ammo readout start together a little to the right and lowered, and the item wheel a little to the right and raised.

### Settings panel

In the original mod the settings panel was too small and soft to read on the Frame.

- Drawn at 1536 pixels with mipmaps, instead of 768 pixels without, so the text stays crisp instead of breaking up (`overlay_vr_resolution`).
- A font made for that size, and text and controls about 1.7 times bigger in view.
- The panel spans 50 degrees instead of 38.
- A text-size slider in the panel makes it bigger or smaller (`overlay_vr_text_scale`).

### Main menu

- The main menu names the build as "LegendFrame 1.0.0 / TRL VR 1.1.0" plus the VR runtime in use.

### Frame install

- One script installs into your Steam copy of the game. It backs up the files it replaces, copies the mod in and keeps the settings you already have on an update.
- It adds a "Tomb Raider: Legend VR" library entry on Proton 11 ARM64, queued for Steam's next start.
- It sets SteamVR to 72 Hz at 1440 per eye for the game, and turns off the game's costliest effects (depth of field, fullscreen effects, shadows, reflections, water).
- It turns off SteamVR's "Auto show game theater" so the game window starts docked in the dashboard and hides with it, instead of floating over the view.
- An uninstall script deletes the mod's files, restores your backups and queues removal of the shortcut.

### Built on Linux

- The mod builds with a mingw toolchain in a container, on the Frame itself. You don't need Windows or Visual Studio.

### Known issues

- The game window starts docked in the SteamVR dashboard. If I still see a floating window, I press "Connect to dock" on it once, or turn off "Auto show game theater" under SteamVR Settings > Dashboard (Advanced). The setting also stops flat apps opening on the theater screen by themselves.
- The game runs through OpenVR, not OpenXR. OpenXR isn't available to 32-bit games under Proton 11 ARM64.
- Turn Next Generation Content off in the game's Setup dialog, or the VR view is black.
- Very busy areas can still dip below 72. The game's own logic runs through the emulator.
- If the buttons behave the old way, SteamVR kept an older binding. Re-pick it under SteamVR Settings > Controllers > Manage Controller Bindings.
- Played on one Steam Frame with the Steam copy of the game.

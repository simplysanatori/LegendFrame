# Changelog

## 1.0.0 (October 2026)

Tomb Raider: Legend in VR on the Valve Steam Frame. LegendFrame is a fork of RototRobot's Tomb-Raider-Legend-VR-Port 1.1.0. All of the VR work is theirs: the stereo view, head tracking, motion controls, the third-person VR camera, first-person mode and god mode. You need your own copy of the game from Steam.

### Why it exists

Tomb Raider: Legend is a 32-bit Windows game. On the Frame it runs through Proton and an x86 emulator, and every draw call costs a lot. The original mod draws the whole scene twice, once per eye. On the Frame that made it CPU-bound at about 40 frames a second, whatever the resolution. LegendFrame makes the game cheap enough to run at the headset's refresh rate.

### Performance

- The original mod ran at about 40 frames a second in gameplay on the Frame. LegendFrame holds the headset's 72 Hz most of the time, with dips to the mid 60s.
- Each frame takes about 14 ms instead of about 25.
- Lowering the resolution never helped the original mod. The time was going into the doubled draws, not the GPU.

### What it adds

- **Single-pass stereo.** Each draw from the game is sent once and lands in both eyes. The original mod sends it twice. The images are the same.
- **Cheaper eye maths.** Both eyes' matrices come from one calculation, and the mod is built with faster maths for the emulator.
- **No idle wait before each frame goes to SteamVR.** The CPU no longer sits waiting for the GPU before handing over a frame.
- **God mode fixed.** In the original mod, picking up a character after a cutscene, or once Lara was wet or dirty, left parts of her stretched back to the pick-up spot. Carries now stay clean.
- **Steam Frame controls.** The Frame controllers have a layout of their own instead of the Oculus Touch one. The d-pad is the game's gear cross (up medipack, right binoculars, down switch weapons, left personal light) in every mode, and it moves through menus. X is precision aim, Y throws grenades, the right Menu button pauses (hold it for the settings panel), the left View button opens the PDA and holding the left bumper does the board-mode finger flick. In first person d-pad down is the game's weapon select as well, and the holsters keep up with it. With the guns holstered in immersive first person, the fire trigger or X draws the pistols straight away (`frame_quick_draw` turns that off), and holding the right bumper puts away whatever is drawn, the pistols to the hips and the long gun to the shoulder (`frame_holster_hold` sets how long). The Frame's cameras lose the controllers at the hips and behind the shoulder, so a button holster is the dependable way, but the reaches still work. The prompts name the Frame's buttons. The left stick's radial gear menu still works. If SteamVR kept the old binding, select the supplied one again under Manage Controller Bindings.
- **Main-menu watermark.** The main menu names the build as "LegendFrame 1.0.0 / TRL VR 1.1.0" plus the VR runtime in use.
- **HUD position.** Sliders move the HUD, and it starts nudged up and to the left.
- **HUD layout.** The health bar, the ammo readout and the item wheel each move as one piece, with a left / right and a down / up slider for each in the settings panel, and two more move the whole HUD. The HUD stays in view while the panel is open, so I can watch them move, and Reset to defaults puts everything back. On the Frame the health bar and the ammo readout start together a little to the right and lowered, and the item wheel a little to the right and raised.
- **Readable settings panel.** In the original mod the settings panel was too small and soft to read on the Frame. Now it's drawn at 1536 pixels with mipmaps and a font made for that size, so the text stays crisp instead of breaking up. The text and controls are about 1.7 times bigger in view, and the panel spans 50 degrees instead of 38. A text-size slider in the panel makes it bigger or smaller.
- **Frame install.** One script installs into your Steam copy of the game. It backs up your files, adds a "Tomb Raider: Legend VR" library entry on Proton 11 ARM64 and sets SteamVR to 72 Hz at 1440 per eye. It also turns off the effects that cost the most. An uninstall script puts everything back.
- **Built on Linux.** The mod builds on the Frame itself. You don't need Windows or Visual Studio.

The original two-eye renderer is still there as a setting (`stereo_mode=twopass`) and a live toggle in the settings panel.

### Known issues

- The game window starts docked in the SteamVR dashboard and hides with it. SteamVR's "Auto show game theater" setting sends every new game window to the theater screen, so the Frame install turns that setting off. If I still see a floating window, I press "Connect to dock" on it once, or turn off "Auto show game theater" under SteamVR Settings > Dashboard (Advanced). The setting also stops flat apps opening on the theater screen by themselves.
- The game runs through OpenVR, not OpenXR. OpenXR isn't available to 32-bit games under Proton 11 ARM64.
- Turn Next Generation Content off in the game's Setup dialog, or the VR view is black.
- Very busy areas can still dip below 72. The game's own logic runs through the emulator.

# Changelog

## 1.0.0 (October 2026)

The first release of LegendFrame: Tomb Raider: Legend in VR on the Valve Steam Frame. It is my Steam Frame version of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) 1.1.0, and all of the VR is theirs: the stereo view, head tracking, motion controls, the third-person VR camera, first-person mode and god mode. You need your own copy of the game from Steam.

### The game on the Frame

- The Steam copy of the game, run through Proton 11 ARM64, in VR through the Frame's SteamVR.
- One install script that backs up the files it replaces and keeps your settings on an update. An uninstall script puts them back.
- A "Tomb Raider: Legend VR" library entry. Start the game from it, not from the store entry.
- SteamVR set to 72 Hz and 1440 per eye for the game, with depth of field, fullscreen effects, shadows, reflections and water turned off.
- SteamVR's "Auto show game theater" turned off, so the game window docks in the dashboard instead of floating over the view.
- The main menu shows "LegendFrame 1.0.0 / TRL VR 1.1.0" and the VR runtime in use.
- The mod builds on the Frame itself, in a mingw container. No Windows needed.

### Performance

The game is 32-bit Windows, so on the Frame every draw goes through an x86 emulator, and the original mod draws the scene twice, once per eye.

- In the original mod, gameplay ran at about 40 fps, 25 ms a frame. Dropping from 1728 to 1440 per eye changed nothing; neither did turning off shadows, reflections and water. The time went into sending every draw twice.
- Now each draw is sent once and lands in both eyes. Gameplay holds the headset's 72 Hz most of the time, at about 14 ms a frame.
- The busiest areas, where the game issues two to three thousand draws a frame, still dip into the 50s. Most of what is left is the game's own logic in the emulator (about 7 ms) and sending the draws (about 3 ms).

How:

- Single-pass stereo in the bundled DXVK: one draw with twice the instances, each sent to one eye by viewport index. The picture matches two-pass, and a draw it cannot take falls back to two-pass by itself.
- Both eyes' matrices come from one calculation, about 47 percent fewer instructions per stereo draw, built with SSE2 maths instead of x87, which the emulator runs much faster.
- The CPU no longer waits for the GPU before handing each frame to SteamVR, which saves about 3 ms a frame (`submit_wait`).
- The original two-pass renderer is still there: `stereo_mode=twopass`, or live from the settings panel or Shift+F6.

### Fixed

What the original mod did, and what this one does instead.

- After a cutscene, a god-mode carry left parts of the character stretched back to where she was picked up. Now carries stay whole.
- Lara's wet and dirty overlay smeared across her belt, boots and shirt while she was carried. Now it stays on her.
- The controls were made for Oculus Touch: on the Frame X and Y did the same as B, three d-pad buttons threw grenades, the right Menu button did nothing and the prompts named buttons the Frame doesn't have. Now every button has its own job.

### Controls

- A jumps, B crouches, rolls and dives, X is precision aim, Y throws grenades and flares.
- The d-pad is the game's gear cross in every mode: up medipack, right binoculars, down switch weapons (in first person too), left personal light.
- Right Menu pauses; holding it opens the settings panel. Left View opens the PDA.
- In first person with the guns holstered, the fire trigger or X draws the pistols.
- Holding the right bumper holsters whatever is drawn, since the Frame's cameras lose the controllers at the hips and behind the shoulder.
- Holding the left bumper does the board-mode flick.
- In the binoculars, X puts them away and then aims.
- When the game won't allow precision aim (mid-jump, hanging, climbing), its own hint box says why.
- The prompts name the Frame's buttons, in English.

The full table is in the [README](../README.md#controls).

### HUD

- The health bar, the ammo readout and the item wheel each move as one piece, with two sliders each. Two more move the whole HUD, and Reset to defaults puts it back.
- The HUD stays in view while the panel is open, so I can watch it move.
- On the Frame the item wheel starts a little right and up, and the health bar and ammo a little right and down.

### Settings panel

In the original mod the panel was too small and soft to read on the Frame.

- Drawn at 1536 pixels with mipmaps instead of 768 without, with a bigger font. Text and controls are about 1.7 times bigger.
- 50 degrees wide instead of 38.
- A text-size slider.

### Known issues

- The game runs through OpenVR, not OpenXR. OpenXR isn't available to 32-bit games under Proton 11 ARM64.
- Turn Next Generation Content off in the game's Setup dialog, or the VR view is black.
- The busiest areas dip into the 50s.

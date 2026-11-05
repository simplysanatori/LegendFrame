# LegendFrame: notes for developers and AI agents

This file holds the technical detail moved out of the README.

## What LegendFrame is

LegendFrame is a fork of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) (TRLVR 1.1.0), the virtual reality mod for Tomb Raider: Legend. All of the VR work is RototRobot's: the stereo view, head tracking, motion controls, the third-person VR camera, first-person mode and god mode. The game is a 32-bit Windows program, so on the Frame it runs through Proton 11 ARM64 and an x86 emulator. LegendFrame makes the mod cheap enough to run at the headset's refresh rate there. The original README is kept as [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Apache License 2.0, as upstream.

## Status

Version 1.0.1 is the current release (the mod is unchanged from 1.0.0; 1.0.1 is a new package). Download it from the [Releases](https://github.com/simplysanatori/LegendFrame/releases) page. What changed is in [docs/CHANGELOG.md](docs/CHANGELOG.md).

LegendFrame 1.0.0 is TRLVR 1.1.0 plus the Frame work in this repository. On the Frame it holds the headset's 72 Hz most of the time in gameplay.

## What this adds to Tomb-Raider-Legend-VR-Port

The original mod draws the whole scene twice, once per eye. On the Frame every draw call goes through the emulator, so that made it CPU-bound at about 40 frames a second, whatever the resolution. LegendFrame changes that and adds a few things for the Frame:

- **Single-pass stereo.** Each draw from the game is sent once and lands in both eyes. The original mod sends it twice. The images are the same. It is done in the bundled DXVK build by viewport index: one draw with twice the instances, each instance routed to one eye's half of the image.
- **Cheaper eye maths.** Both eyes' matrices come from one calculation, and the mod is built with faster maths for the emulator.
- **No idle wait before each frame goes to SteamVR.** The CPU no longer sits waiting for the GPU before handing over a frame.
- **God mode fixed.** In the original mod, picking up a character after a cutscene, or once Lara was wet or dirty, left parts of her stretched back to the pick-up spot. Carries now stay clean.
- **Steam Frame controls.** The Frame controllers have a layout of their own. A, B, X and Y are on the right hand, each with its own job: X is precision aim and Y throws grenades. The d-pad is the game's gear cross (up medipack, right binoculars, down switch weapons, left personal light) and it moves through menus. The right Menu button pauses (hold it for the settings panel), the left View button opens the PDA and holding the left bumper does the board-mode finger flick. The prompts name the Frame's buttons.
- **HUD position.** Sliders move the HUD, and it starts nudged up and to the left.
- **HUD layout.** The health bar, the ammo readout and the item wheel each move as one piece, with a left / right and a down / up slider for each in the settings panel, and two more move the whole HUD. The HUD stays in view while the panel is open, so I can watch them move, and Reset to defaults puts everything back. On the Frame the health bar and the ammo readout start together a little to the right and lowered, and the item wheel a little to the right and raised.
- **Readable settings panel.** In the original mod the settings panel was too small and soft to read on the Frame. Now it's drawn at 1536 pixels with mipmaps and a font made for that size, so the text stays crisp instead of breaking up. The text and controls are about 1.7 times bigger in view, and the panel spans 50 degrees instead of 38. A text-size slider in the panel makes it bigger or smaller.
- **Frame install.** One script installs into your Steam copy of the game. It backs up your files, adds a "Tomb Raider: Legend VR" library entry on Proton 11 ARM64 and sets SteamVR to 72 Hz at 1440 per eye. It also turns off the effects that cost the most. An uninstall script puts everything back.
- **Built on Linux.** The mod builds on the Frame itself. You don't need Windows or Visual Studio.

The original two-eye renderer is still there as a setting (`stereo_mode=twopass`) and a live toggle in the settings panel. Everything else is upstream's.

## Performance internals

Lowering the resolution never helped the original mod. The time was going into the doubled draws, not the GPU. The rest of each frame is mostly the game's own logic running through the emulator.

The measurements, the FEX notes and the OpenVR / OpenXR findings are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md#performance-what-is-measured). The user-facing numbers are in [docs/USER_GUIDE.md](docs/USER_GUIDE.md#performance).

## Design documents

- [docs/DESIGN.md](docs/DESIGN.md): single-pass stereo (SPS-VI), the DXVK fork, the proxy interface, risks and test plan.
- [docs/DESIGN-frame-controls.md](docs/DESIGN-frame-controls.md): the Frame controller layout and the owner's decisions as implemented.
- [docs/PLAN.md](docs/PLAN.md): the work plan by milestone and lane.
- [docs/INVESTIGATION-godmode-stretch.md](docs/INVESTIGATION-godmode-stretch.md): the god-mode carry stretch.
- [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md): what each installer step does, options, manual steps, tuning and log lines.

Developer notes and what was learned about SteamVR on the Frame are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md) and [docs/DESIGN.md](docs/DESIGN.md).

## Build from source

The mod is cross-built for 32-bit Windows (the game is 32-bit) with mingw in a podman container, on the Frame itself. `toolchain/build.sh <source> <output>` produces `d3d9.dll`, `d3d9_dxvk.dll`, `trlvr_launcher.exe` and `openxr_loader.dll`. With `third_party/openvr/bin/win32/openvr_api.dll`, `input/actions.json` (as `trlvr_actions.json`) and the `input/bindings_*.json` files they make the folder `frame/install.sh --payload` takes. `toolchain/stereotest.sh` checks single-pass against two-pass image for image. Upstream's Windows build (`build.bat`, MSVC) is still in the tree. The design is in [docs/DESIGN.md](docs/DESIGN.md).

## Licence detail

Third-party code keeps its own licences (DXVK is zlib/libpng and the shipped `d3d9_dxvk.dll` is a modified build).

## Credits detail

**RototRobot**, author of [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod this is a fork of, including the stereo renderer, tracking, controls, god mode, physics and settings panel. Without their work there would be nothing to port.

## How this was made (longer)

I wanted Tomb Raider: Legend in VR on my Frame, so I took RototRobot's mod and worked on making it cheaper to run there. The game is a 32-bit x86 program emulated under Proton, and the limit turned out to be the cost of each draw call, not the GPU. The work was a single-pass stereo renderer that draws both eyes at once, cheaper per-eye maths, a Frame installer and controller binding, and tuning against measurements from the headset.

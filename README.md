# LegendFrame

Tomb Raider: Legend in VR, running on the Valve Steam Frame.

LegendFrame is a fork of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) (TRLVR 1.1.0), the virtual reality mod for Tomb Raider: Legend. All of the VR work is RototRobot's: the stereo view, head tracking, motion controls, the third-person VR camera, first-person mode and god mode. The game is a 32-bit Windows program, so on the Frame it runs through Proton 11 ARM64 and an x86 emulator. LegendFrame makes the mod cheap enough to run at the headset's refresh rate there. The original README is kept as [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Apache License 2.0, as upstream.

You need your own copy of Tomb Raider: Legend from Steam. No game data is in this repository.

## Status

Version 1.0.0 is released. Download it from the [Releases](https://github.com/simplysanatori/LegendFrame/releases) page. What changed is in [docs/CHANGELOG.md](docs/CHANGELOG.md).

LegendFrame 1.0.0 is TRLVR 1.1.0 plus the Frame work in this repository. On the Frame it holds the headset's 72 Hz most of the time in gameplay.

Known issues:

- The game window opens as a floating 2D screen over the VR view. Press "Connect to dock" on it once and it gets out of the way.
- The game runs through OpenVR, not OpenXR. OpenXR isn't available to 32-bit games under Proton 11 ARM64.
- Turn Next Generation Content off in the game's Setup dialog, or the VR view is black.
- Very busy areas can still dip below 72. The game's own logic runs through the emulator.

## What this adds to Tomb-Raider-Legend-VR-Port

The original mod draws the whole scene twice, once per eye. On the Frame every draw call goes through the emulator, so that made it CPU-bound at about 40 frames a second, whatever the resolution. LegendFrame changes that and adds a few things for the Frame:

- **Single-pass stereo.** Each draw from the game is sent once and lands in both eyes. The original mod sends it twice. The images are the same. It is done in the bundled DXVK build by viewport index: one draw with twice the instances, each instance routed to one eye's half of the image.
- **Cheaper eye maths.** Both eyes' matrices come from one calculation, and the mod is built with faster maths for the emulator.
- **No idle wait before each frame goes to SteamVR.** The CPU no longer sits waiting for the GPU before handing over a frame.
- **God mode fixed.** In the original mod, picking up a character after a cutscene, or once Lara was wet or dirty, left parts of her stretched back to the pick-up spot. Carries now stay clean.
- **Steam Frame controllers.** A native binding. Tap the left menu button for the gear menu and pause. Hold it for the settings panel.
- **HUD position.** Sliders move the HUD, and it starts nudged up and to the left.
- **HUD layout.** The item wheel and the ammo readout each move as one piece, with a left / right and a down / up slider in the settings panel. The HUD stays in view while the panel is open, so I can watch them move. On the Frame they start a little to the right, the item wheel raised and the ammo readout lowered.
- **Readable settings panel.** In the original mod the settings panel was too small and soft to read on the Frame. Now it's drawn at 1536 pixels with mipmaps and a font made for that size, so the text stays crisp instead of breaking up. The text and controls are about 1.7 times bigger in view, and the panel spans 50 degrees instead of 38. A text-size slider in the panel makes it bigger or smaller.
- **Frame install.** One script installs into your Steam copy of the game. It backs up your files, adds a "Tomb Raider: Legend VR" library entry on Proton 11 ARM64 and sets SteamVR to 72 Hz at 1440 per eye. It also turns off the effects that cost the most. An uninstall script puts everything back.
- **Built on Linux.** The mod builds on the Frame itself. You don't need Windows or Visual Studio.

The original two-eye renderer is still there as a setting (`stereo_mode=twopass`) and a live toggle in the settings panel. Everything else is upstream's.

## Requirements

- A Steam Frame with SteamOS and SteamVR, logged into Steam.
- **Tomb Raider: Legend** (Steam app 7000), installed from the Steam store. Start it once from the store entry so its Wine prefix and Setup values exist.
- **Proton 11 (ARM64)** installed in Steam. The installer sets the game and its shortcut to the tool named `proton_11-arm64`. Check it under Steam > Settings > Compatibility if the name differs.
- `legendframe-1.0.0-payload.zip` from the [Releases](https://github.com/simplysanatori/LegendFrame/releases) page.
- A terminal on the Frame: SSH, or Konsole from desktop mode.

## Install

1. Download `legendframe-1.0.0-payload.zip` and `SHA256SUMS` from the [Releases](https://github.com/simplysanatori/LegendFrame/releases) page, and check the zip:

   ```sh
   cd ~/Downloads
   sha256sum -c --ignore-missing SHA256SUMS
   ```

2. Get the repository. It has the installer:

   ```sh
   git clone -b steam-frame https://github.com/simplysanatori/LegendFrame.git
   cd LegendFrame
   ```

3. Quit the game if it runs, then install the zip into the Steam copy of the game:

   ```sh
   frame/install.sh --payload ~/Downloads/legendframe-1.0.0-payload.zip
   ```

   The installer finds the game, backs up any mod files already there and copies the mod in. It seeds the Frame settings in `trlvr.ini` and the game's graphics registry values. It sets SteamVR's per-app settings (72 Hz, resolution 1440) when SteamVR is running. It queues the **Tomb Raider: Legend VR** shortcut for Steam's next start. Values you already have are kept. It needs no root and can be run again. `frame/install.sh --help` lists the options. `frame/install.sh --check` prints the installed version.

4. Restart Steam yourself (Power > Exit Steam, then start Steam again). The installer never does it. On that start Steam gets the shortcut **Tomb Raider: Legend VR**, set to Proton 11 ARM64, with the store art.

5. Put the headset on and launch **Tomb Raider: Legend VR**, not the store entry. The shortcut starts the game as a VR app through `trlvr_launcher.exe`. The store entry starts it flat.

[docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md) has what each installer step does, and the manual steps if the installer cannot do one.

To update, run the installer again with the new zip. To remove LegendFrame, run `frame/uninstall.sh`. It deletes the mod's files, restores your backups and queues removal of the shortcut for the next Steam start. It leaves the registry values, the SteamVR per-app settings and the Proton mapping for app 7000 alone, and prints how to revert each.

## First run

On the first run the game shows its **Setup** dialog. You can run it once from the store entry before installing, so the registry section exists. In it:

- Turn **Next Generation Content off**. Its separate renderer draws a black scene in VR, and no registry value controls it, so the installer cannot do this for you.
- The other options (Depth of Field, Fullscreen Effects, Shadows, Reflections, Water) are set by the installer and the mod. The mod overrides Depth of Field and Fullscreen Effects at every start.
- The dialog should not come back unless the graphics adapter changes. If it does, the same answers apply.

If the registry section did not exist when you installed, the installer says so: run the game once and run the installer again.

## Controls

The layout is upstream's: see [VR CONTROLLER BINDINGS.txt](release/VR%20CONTROLLER%20BINDINGS.txt). Oculus Touch is the layout that applies on the Frame: the bindings ship as `bindings_*.json`, and SteamVR remaps the Oculus Touch binding onto the Frame controllers.

One thing is different on the Frame. The **left menu button** is the Frame controller's own menu button, and LegendFrame binds it directly: a tap opens the gear menu and pauses, and holding it for about half a second opens the settings panel. The left system button always opens the SteamVR dashboard and is never bound.

If a button does nothing, open SteamVR > Settings > Controllers > Manage Controller Bindings and reset the choice for Tomb Raider: Legend.

## Settings

Edit `trlvr.ini` in the game folder with the game closed. The mod writes every key with its explanation the first time it runs. The settings panel (hold the left menu button) changes most of them live. The ones that matter on the Frame, with the values a Frame install starts with:

| Key | Default | What it does |
|---|---|---|
| `stereo_mode` | `singlepass` | `singlepass` sends each draw once for both eyes. `twopass` is the original mod's renderer. The settings panel and Shift+F6 switch it live |
| `submit_wait` | `0` | `1` makes the CPU wait for the GPU before each frame goes to SteamVR, as the original mod did. Slower |
| `god_carry_fix` | `1` | Keeps carried characters whole in god mode. `0` is the original carry |
| `vr_runtime` | `openvr` | The installer sets `openvr`. `openxr` leaves the game flat under Proton 11 ARM64 |
| `hud_offset_x`, `hud_offset_y` | `-0.08`, `0.08` | Moves the gameplay HUD. Also sliders in the settings panel |
| `hud_items_x`, `hud_items_y` | `0.20`, `0.20` | Moves the whole item wheel (the d-pad gear cross, every slot), in the same units as `hud_offset`. Sliders in the settings panel, where the HUD stays in view while you move them. `hud_items_hide` (`0`) hides it |
| `hud_ammo_x`, `hud_ammo_y` | `0.20`, `-0.20` | Moves the weapon and ammo readout, icon and numbers together, the same way. `hud_ammo_hide` (`0`) hides it |
| `hud_debug` | `0` | `1` logs the HUD's parts and which element each belongs to, if a slider moves the wrong thing |
| `overlay_vr_width_degrees` | `50` | How wide the settings panel looks in the headset. The original mod used 38, which an install moves to 50. Also a slider in the panel |
| `overlay_vr_text_scale` | `1.25` | Text and control size in the settings panel. Larger fits less at once. Also a slider in the panel; it applies the next time the panel opens |
| `overlay_vr_resolution` | `1536` | The settings panel's texture size in pixels. Higher is sharper when you lean in |
| `stereo_stats` | `0` | `1` logs a `stereo stats:` line every 5 seconds |
| `fsaa` | `off` | The game's anti-aliasing is expensive |
| `comfort_vignette` | `off` | `jumps` or `full` darken the edge of view while moving |
| `cull_fov_degrees` | `110` | Objects outside this angle are not drawn. Lower is cheaper but may pop |
| `stereo_same_frame` | `1` | Keep at `1`. `0` draws alternate eyes, which doubles moving images |

Outside the ini: the per-app SteamVR refresh rate and resolution (72 Hz, 1440). The mod reads the resolution at start-up, so restart the game after changing it. Shadows, reflections and water are off in the game's registry values.

## Performance

On one Steam Frame at 72 Hz, 1440 pixels per eye, Proton 11 ARM64 and OpenVR, with shadows, reflections and water off:

| | Original mod | LegendFrame |
|---|---|---|
| Gameplay frame rate | about 40 fps | 72 fps most of the time, dips to the mid 60s |
| Time per frame | about 25 ms | about 14 ms |

Lowering the resolution never helped the original mod. The time was going into the doubled draws, not the GPU. The rest of each frame is mostly the game's own logic running through the emulator. Let the headset cool between long sessions: a hot Frame throttles and drops the frame rate.

## Files

| What | Where |
|---|---|
| Mod files | the game folder, `~/.local/share/Steam/steamapps/common/Tomb Raider Legend/`: `d3d9.dll`, `d3d9_dxvk.dll`, `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json`, `bindings_*.json`, `openxr_loader.dll` |
| Settings | `trlvr.ini` in the game folder |
| Install record and version | `.legendframe-files.txt` and `.legendframe-version` in the game folder |
| Backups of replaced files | `<game>/.legendframe-backup-<timestamp>/` |
| Registry backup | `compatdata/7000/pfx/user.reg.legendframe-bak-<timestamp>` |
| Generated at run time | `trl_vr.exe` (the large-address-aware copy), logs, crash dumps, all in the game folder |
| Shortcut helper | `~/.local/share/legendframe/shortcut/` |
| Steam drop-in that adds the shortcut | `~/.config/systemd/user/steam.service.d/60-legendframe.conf` (removes itself after one Steam start) |

The game's own files are never modified, so Steam's "Verify integrity" never needs to repair anything.

## Troubleshooting

- **The game starts flat, not in VR.** Launch the **Tomb Raider: Legend VR** shortcut, not the store entry. Check that `trlvr.ini` has `vr_runtime=openvr`.
- **A floating 2D window covers the VR view.** That is the game window. Press "Connect to dock" on it.
- **The scene is black in VR.** Turn **Next Generation Content off** in the game's Setup dialog.
- **A controller button does nothing.** Reset the binding in SteamVR > Settings > Controllers > Manage Controller Bindings for Tomb Raider: Legend. The left system button always opens the SteamVR dashboard.
- **The installer says the registry section does not exist.** Run the game once from the store entry, then run the installer again.
- **Single-pass is not working.** `trlvr.log` has `stereo: single-pass (viewport index) enabled, DXVK stereo interface v1` when it is up. If it says `stereo: DXVK has no single-pass interface` or `EnableSinglePassStereo failed` instead, the game runs two-pass: reinstall so `d3d9.dll` and `d3d9_dxvk.dll` come from the same release.
- **Frames look torn or incomplete.** Set `submit_wait=1` and tell me what you saw.
- **Frame rate is lower than expected.** Look at the `perf:` lines in `trlvr.log`: they say whether a slow area is CPU- or GPU-bound. Let the headset cool, and run nothing else on it.
- **Roll back.** Run `frame/uninstall.sh`. Steam's "Verify integrity" also works, as the game's own files are never changed.

## Reporting a problem

Open an [issue](https://github.com/simplysanatori/LegendFrame/issues) with what you did, what you expected and what happened, and the logs. They are all in the game folder:

- `trlvr.log` (and `trlvr.log.prev`): the headset, hooks, controllers, and a `perf:` line every five seconds with frame rate, GPU and CPU time, draws and throttling. Pull out the `perf:` lines first.
- `trlvr_launcher.log` and `trl_vr_d3d9.log`: the launcher and the DXVK side.
- `trlvr_crash.dmp` (and `.prev`) after a crash.
- SteamVR's `vrserver.txt` and `vrcompositor.txt` under `~/.local/share/Steam/logs/`.

Look through them for anything personal before you share them.

## Build from source

The mod is cross-built for 32-bit Windows (the game is 32-bit) with mingw in a podman container, on the Frame itself. `toolchain/build.sh <source> <output>` produces `d3d9.dll`, `d3d9_dxvk.dll`, `trlvr_launcher.exe` and `openxr_loader.dll`. With `third_party/openvr/bin/win32/openvr_api.dll`, `input/actions.json` (as `trlvr_actions.json`) and the `input/bindings_*.json` files they make the folder `frame/install.sh --payload` takes. `toolchain/stereotest.sh` checks single-pass against two-pass image for image. Upstream's Windows build (`build.bat`, MSVC) is still in the tree. The design is in [docs/DESIGN.md](docs/DESIGN.md).

## Support

LegendFrame exists because of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port). If you want to support someone, support them first: star and follow it, report what you find there, and send improvements to the mod upstream.

If you would like more Steam Frame ports, you can also [buy me a coffee](https://ko-fi.com/sana748653). It pays for the Claude API credits these ports are built with, spent as carefully as I can.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- **RototRobot**, author of [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod this is a fork of, including the stereo renderer, tracking, controls, god mode, physics and settings panel. Without their work there would be nothing to port.
- DXVK, by Philip Rebohle and contributors.
- Valve's OpenVR.
- Khronos OpenXR.
- Dear ImGui.
- chreden's TRAWindowed.
- Crystal Dynamics, for the game.

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists the third-party parts and their licences.

Tomb Raider: Legend is the property of its owners. This is an unofficial fan project, not affiliated with or endorsed by Crystal Dynamics, Eidos, Valve or the upstream author.

## Licence

Apache License 2.0, as upstream: see [LICENSE](LICENSE) and [NOTICE](NOTICE). Third-party code keeps its own licences (DXVK is zlib/libpng and the shipped `d3d9_dxvk.dll` is a modified build).

## How this was made

I wanted Tomb Raider: Legend in VR on my Frame, so I took RototRobot's mod and worked on making it cheaper to run there. The game is a 32-bit x86 program emulated under Proton, and the limit turned out to be the cost of each draw call, not the GPU. The work was a single-pass stereo renderer that draws both eyes at once, cheaper per-eye maths, a Frame installer and controller binding, and tuning against measurements from the headset.

This is vibe coded, with manual review throughout. I wrote it with Claude Code, using Anthropic's Fable 5.1, Opus 5.5 and Sonnet 5.5 models running on the Frame itself. I chose what to build, reviewed the code and the scripts as they were written, tested each build in the headset and reported what I saw; Claude read the logs and changed the code. Upstream's README says it was made with heavy AI assistance, so the code underneath is AI assisted too. RototRobot has not reviewed this fork. If that is not for you, no worries. Read the code before trusting it.

Developer notes and what was learned about SteamVR on the Frame are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md) and [docs/DESIGN.md](docs/DESIGN.md).

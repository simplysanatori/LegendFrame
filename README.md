# LegendFrame

Tomb Raider: Legend in VR, running on the Valve Steam Frame.

LegendFrame is a fork of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) (TRLVR 1.1.0), the virtual reality mod for Tomb Raider: Legend with stereoscopic rendering, 6DoF head tracking, motion controllers, a VR third-person camera and an optional first-person mode. All of the VR work is RototRobot's. The game is a closed-source 32-bit x86 Windows program, so on the Frame (an Arm64 Linux headset) it runs through Proton 11 ARM64 and SteamVR, emulated. LegendFrame adapts the mod to that setup, and adds a single-pass stereo renderer to make it cheaper to emulate. The original README is kept as [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Apache License 2.0, as upstream.

You need your own copy of Tomb Raider: Legend from Steam. No game data is in this repository.

## Status

Version 0.1.3, in testing on the Frame. There is no GitHub release yet. LegendFrame 0.1.x is upstream mod 1.1.0 plus the Frame work in this repository.

Measured on one Frame (72 Hz, 1440 pixels per eye, Proton 11 ARM64, OpenVR):

- Upstream's two-pass stereo: about 40 frames a second median in gameplay.
- LegendFrame 0.1.1 single-pass: about 51 frames a second median in gameplay (worst 27 to 39, best 72).
- Menu A/B with the CPU wait for the GPU before `Submit` removed (`submit_wait=0`): 43 to 45 frames a second became 67 to 70.

Known issues and what is not confirmed yet:

- The game does not hold the frame rate the headset can show. It runs, and is faster than upstream, but not at 72 in gameplay.
- I have not yet confirmed the gameplay effect of `submit_wait=0`, or whether it shows tearing or partial frames in the headset.
- I have not yet confirmed that the `god_carry_fix` option cures the god-mode carry smearing.
- OpenXR is not available under Proton 11 ARM64, so the Frame runs the mod through OpenVR.

The game is a 32-bit x86 program, so all of it is emulated. The cost that limits it is the per-draw submission, not the GPU. The numbers and the reasoning are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md#performance-what-is-measured).

## What this adds to Tomb-Raider-Legend-VR-Port

Upstream runs on Windows. On the Frame the game goes through Proton and an emulator, where every draw call is expensive. I changed the platform side and added a few things to the mod:

- **Single-pass stereo.** One D3D9 draw for both eyes instead of two, done in the DXVK fork by the viewport-index technique (not Vulkan multiview): the draw is issued once with `instanceCount` doubled, a vertex-shader epilogue sets `ViewportIndex = InstanceIndex & 1`, and the per-eye `c0..c3` matrices come from a left/right bank. The images and overlays stay the existing double-wide target. The two-pass path is kept as a fallback: `[vr] stereo_mode=twopass|singlepass`, and a live **Shift+F6** or settings-panel toggle. Design in [docs/DESIGN.md](docs/DESIGN.md), plan in [docs/PLAN.md](docs/PLAN.md).
- **Cheaper per-eye maths.** The proxy computes both eyes' matrices in one call (`stereo_shared_math`, bit-exact against the two-call version) and is built with SSE2 maths.
- **No GPU wait before Submit (option).** `[vr] submit_wait=0` drops the CPU wait for the GPU before the OpenVR submit. Under trial.
- **God-mode carry fix (option).** `[vr] god_carry_fix=1` addresses an upstream carry bug where parts of Lara's mesh stay at the pick-up spot; see [docs/INVESTIGATION-godmode-stretch.md](docs/INVESTIGATION-godmode-stretch.md). Under trial.
- **Linux build.** The 32-bit `d3d9.dll` proxy, the DXVK fork and the launcher are cross-built with mingw in a container (`toolchain/`), on the Frame itself, instead of MSVC on Windows. The toolchain includes an SEH shim (mingw has no 32-bit SEH) and SSE2 flags.
- **Frame installer.** `frame/install.sh` and `frame/uninstall.sh` install into the Steam copy of the game on SteamOS, with backups, a manifest and a queued Steam shortcut.
- **Frame controller binding.** A native Frame controller binding: the left menu button taps for the gear menu and pause, and holds for the settings panel.
- **HUD nudge.** `hud_offset_x` and `hud_offset_y` move the gameplay HUD (also sliders in the settings panel).
- **Frame tuning defaults.** FSAA off, MSAA 2, no comfort vignette, a 110 degree culling cone, no shadows, reflections or water effects, 72 Hz at resolution 1440 in SteamVR, and the OpenVR runtime.

Everything else is upstream's.

## Requirements

- A Steam Frame with SteamOS and SteamVR, logged into Steam.
- **Tomb Raider: Legend** (Steam app 7000), installed from the Steam store. The installer needs `trl.exe` in the Steam library, and the game should have been started once, so that its Wine prefix exists.
- **Proton 11 (ARM64)** installed in Steam. The installer sets the game and its shortcut to the tool named `proton_11-arm64`. Check it under Steam > Settings > Compatibility if the name differs.
- A built LegendFrame payload: a folder containing `d3d9.dll`, `d3d9_dxvk.dll`, `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json` and the `bindings_*.json` files. Build it with the container toolchain in `toolchain/` (it runs on the Frame itself); there is no release package yet. The mingw-built `d3d9.dll` links `openxr_loader.dll`, so that file must be in the payload too.
- A terminal on the Frame: SSH, or Konsole from desktop mode.

## Install

1. Get the repository:

   ```sh
   git clone https://github.com/simplysanatori/LegendFrame.git
   cd LegendFrame
   git checkout steam-frame
   ```

2. Install the payload into the Steam copy of the game:

   ```sh
   frame/install.sh --payload /path/to/dist
   ```

   The installer finds the game, backs up any mod files already there, copies the payload, seeds Frame defaults for `trlvr.ini` and the game's graphics registry values, sets SteamVR's per-app settings (72 Hz, resolution 1440) when SteamVR is running, and queues the **Tomb Raider: Legend VR** shortcut for Steam's next start. It needs no root, can be run again, and refuses to run while the game is running. Pass `--with-openxr` for payloads built with the mingw toolchain. Other options: `--userid N`, `--no-registry`, `--no-vrsettings`, `--no-shortcut`, `--edit-vrsettings`, `--steam-dir` and `--game-dir`.

3. Restart Steam yourself (Power > Exit Steam, then start Steam again). The installer never does it. On that start the queued drop-in adds the non-Steam shortcut **Tomb Raider: Legend VR**, maps the shortcut and app 7000 to Proton 11 ARM64, copies the store art, and deletes itself.

4. Put the headset on and launch the **Tomb Raider: Legend VR** shortcut, not the store entry. The shortcut carries the OpenVR flag, so SteamVR treats it as a VR app, and it runs `trlvr_launcher.exe`, which makes a large-address-aware copy of the game (`trl_vr.exe`) and starts it. The store entry runs the plain `trl.exe` and starts flat.

[docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md) has what each installer step does and the manual steps if the installer cannot do one (files, shortcut, Proton for the store entry, per-app SteamVR settings, registry).

To remove LegendFrame, run `frame/uninstall.sh`. It refuses to run while the game runs, deletes the files listed in `.legendframe-files.txt`, the files the mod generated, and `trlvr.ini` if the installer created it, restores your backups, and queues removal of the shortcut for the next Steam start. It leaves the registry values, the SteamVR per-app settings and the Proton mapping for app 7000 alone and prints how to revert each.

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

Edit `trlvr.ini` in the game folder with the game closed. The file explains every key. The installer seeds only `fsaa`, `msaa`, `comfort_vignette` and `cull_fov_degrees`, and only if there is no ini. An existing ini is never touched, so the keys LegendFrame adds are written by hand for now, under `[vr]`. The ones that matter on the Frame:

| Key | Code default | Frame setting | What it does |
|---|---|---|---|
| `stereo_mode` | `twopass` | `singlepass` | `singlepass` issues one draw for both eyes; `twopass` is upstream's path and the fallback. Shift+F6 or the settings-panel checkbox flips it live (not saved) |
| `stereo_stats` | `0` | `1` | Logs a `stereo stats:` line every 5 s: single-pass and fallback draw counts and timing. Cheap |
| `stereo_shared_math` | `1` | `1` | Both eyes' matrices in one call (bit-exact). `0` gives the two legacy calls, for A/B |
| `submit_wait` | `1` | `0` (under trial) | `0` is no CPU wait for the GPU before `Submit`. Menu gain measured; gameplay and tearing pending. Set `1` if anything looks wrong. The OpenXR path is unaffected |
| `god_carry_fix` | `1` | `1` (under trial) | Fix for upstream god-mode carry smearing when Lara is drawn with a second model. `0` is the 1.1.0 carry, for A/B |
| `hud_offset_x`, `hud_offset_y` | `0`, `0` | `-0.08`, `0.08` | Nudges the gameplay HUD only (NDC units). Also sliders in the settings panel |
| `vr_runtime` | `auto` | `openvr` | `openxr` does not work under Proton 11 ARM64; see Troubleshooting |
| `stereo_same_frame` | `1` | `1` | Both eyes in one frame. `0` is alternate-eye: faster, but unusable (doubled images) |
| `fsaa` | `off` | `off` | The game's anti-aliasing is expensive |
| `msaa` | `2` | `2` | Used only when `fsaa` is on |
| `comfort_vignette` | | `off` | `jumps` or `full` darken the edge of view while moving |
| `cull_fov_degrees` | | `110` | Objects outside this angle are not drawn. Lower is cheaper but may pop |
| `first_person_object_culling`, `terrain_culling` | | `frustum` | Keep at `frustum`; `open` is slow |
| `ui_scale`, `hud_follow` | | `0.6`, `0.85` | HUD size and how it follows your head |
| `vr_capture_c0` | `0` | `0` | Debug: records the game's `c0` and state for the maths test. Leave off |

Outside the ini: the per-app SteamVR refresh rate and resolution (72 Hz, 1440, set by the installer; the mod reads the resolution at start-up, so restart the game after changing it), and the game's Setup dialog for Shadows, Reflections and Water (the installer turns them off in the registry).

## Performance

All numbers are from one Steam Frame at 72 Hz, 1440 pixels per eye, Proton 11 ARM64, OpenVR, with shadows, reflections and water off. My own play sessions supplied the gameplay figures.

| Setup | Result | Status |
|---|---|---|
| Upstream 1.0.0, two-pass stereo, 1728 px per eye | gameplay median about 39 fps | measured |
| Upstream two-pass, 1440 px per eye | gameplay median about 40 fps, 2000 draws per frame | measured |
| `stereo_same_frame=0` (alternate-eye) | median 68 fps, but unusable (doubled images) | measured |
| LegendFrame 0.1.1, single-pass | gameplay median about 51 fps (worst 27 to 39, best 72), frame about 19.8 ms, about 1450 draws, fallback and rejected draws 0 | measured |
| Menu, `submit_wait=1` (GPU wait kept) | 43 to 45 fps, 23 ms frame, 9 ms at Present | measured |
| Menu, `submit_wait=0` (no GPU wait) | 67 to 70 fps, 14.5 ms frame, 1 ms at Present | measured, menu only |
| Gameplay with `submit_wait=0` | not yet known | pending my test |
| Tearing or partial frames with `submit_wait=0` | not yet known | pending my test |
| God-mode carry smearing with `god_carry_fix=1` | not yet known | pending my test |

What this tells us:

- The game is not limited by resolution or by the GPU. Going from 1728 to 1440 pixels changed nothing, and cutting a quarter of the draws changed GPU time very little.
- The CPU side is the limit. Each D3D9 draw goes through the emulated game, the emulated D3D9 front end of DXVK and the Vulkan translation layers. Upstream issues every draw twice, once per eye. Alternate-eye rendering (half the draws per frame) reached 68 frames a second and confirmed it.
- Single-pass stereo answers that: one D3D9 draw and one `vkCmdDraw` for both eyes. Draws it cannot take (instanced, fixed-function, per-eye textures, reflection pass) fall back to two-pass per draw. The images match two-pass; the headless harness checks this with a maximum difference of 0.
- The proxy was first built with x87 maths (the mingw i686 default) and is now built with SSE2, which matters under FEX. The per-eye matrix work is also done for both eyes in one call (bit-exact against two calls on 8460 synthetic and 13207 captured sets), at about 47 percent fewer instructions per stereo draw.
- `submit_wait=0` skips the CPU wait for the GPU before the OpenVR `Submit`. The order is still guaranteed by the submission-queue lock and the recorded layout barrier (the same pattern DXVK's own D3D11 OpenVR interop uses), so no tearing is expected, but that is what the headset test is for.
- Heat matters a lot. A 0.1.2 session read a 36 frames a second median that was the Frame throttling while another job had just run the headset hard; the same code gave 51 cool. Let the headset cool before you compare numbers, and run nothing else on it while testing.
- FEX is already configured for the game (x87 reduced precision, TSO and multiblock on) in Proton's `Config.json`. Do not set `FEX_SMCCHECKS=none`: the mod patches game code at run time. Turning TSO off is risky.

Not done yet: the remaining per-draw hook overhead (other-instance draws about 2 to 3 ms, terrain about 1 ms in the `perf cpu:` lines), and fewer constant-change arm calls. A native Arm64 port of the game is not possible: there is no ARM64 target for 32-bit Windows code.

## Files

| What | Where |
|---|---|
| Mod files | the game folder, `~/.local/share/Steam/steamapps/common/Tomb Raider Legend/`: `d3d9.dll`, `d3d9_dxvk.dll`, `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json`, `bindings_*.json`, and `openxr_loader.dll` with `--with-openxr` |
| Settings | `trlvr.ini` in the game folder |
| Install record | `.legendframe-files.txt` in the game folder |
| Backups of replaced files | `<game>/.legendframe-backup-<timestamp>/` |
| Registry backup | `compatdata/7000/pfx/user.reg.legendframe-bak-<timestamp>` |
| Generated at run time | `trl_vr.exe` (the large-address-aware copy), logs, crash dumps, all in the game folder |
| Shortcut helper | `~/.local/share/legendframe/shortcut/` |
| Steam drop-in that adds the shortcut | `~/.config/systemd/user/steam.service.d/60-legendframe.conf` (removes itself after one Steam start) |

The game's own files are never modified, so Steam's "Verify integrity" never needs to repair anything.

## Troubleshooting

- **The game starts flat, not in VR.** Launch the **Tomb Raider: Legend VR** shortcut, not the store entry. Check `vr_runtime=openvr` in `trlvr.ini`. OpenXR is not available under Proton 11 ARM64 for this mod: `xrCreateInstance` returns `XR_ERROR_RUNTIME_UNAVAILABLE`, and forcing `vr_runtime=openxr` leaves the game flat. `auto` wastes several seconds before falling back to OpenVR. Under Proton Experimental (ARM64) OpenXR works, but its menu frame rate was lower (32 to 40 fps against 42 on Proton 11, two runs each). It is untested in gameplay.
- **The scene is black in VR.** Turn **Next Generation Content off** in the game's Setup dialog. Its separate renderer draws black in VR.
- **A controller button does nothing.** Reset the binding in SteamVR > Settings > Controllers > Manage Controller Bindings for Tomb Raider: Legend. The left system button always opens the SteamVR dashboard.
- **The installer says the registry section does not exist.** Run the game once, then run the installer again.
- **Single-pass is not working.** `trlvr.log` has `stereo: single-pass (viewport index) enabled, DXVK stereo interface v1` when it is up. Bad signs instead are `stereo: DXVK has no single-pass interface`, `stereo draw rejected`, `stereo lowering failed` and `EnableSinglePassStereo failed`; the game then runs two-pass.
- **Something looks wrong with `submit_wait=0`.** Set `submit_wait=1` and tell me what you saw.
- **Frame rate is lower than expected.** Look at the `perf:` lines in `trlvr.log`: they say whether a slow area is CPU- or GPU-bound. Let the headset cool, and run nothing else on it.
- **Roll back.** Run `frame/uninstall.sh`. Your backups come back from `.legendframe-backup-*`. Steam's "Verify integrity" also works, as the game's own files are never changed.

## Reporting a problem

Open an [issue](https://github.com/simplysanatori/LegendFrame/issues) with what you did, what you expected and what happened, and the logs. They are all in the game folder:

- `trlvr.log` (and `trlvr.log.prev`): the headset, hooks, controllers, and a `perf:` line every five seconds with frame rate, GPU and CPU time, draws and throttling. Pull out the `perf:` lines first.
- `trlvr_launcher.log` and `trl_vr_d3d9.log`: the launcher and the DXVK side.
- `trlvr_crash.dmp` (and `.prev`) after a crash.
- SteamVR's `vrserver.txt` and `vrcompositor.txt` under `~/.local/share/Steam/logs/`.

Look through them for anything personal before you share them.

## Build from source

The mod is cross-built for 32-bit Windows (the game is 32-bit) with mingw in a podman container. The recipe is in `toolchain/`: it produces `d3d9.dll`, `d3d9_dxvk.dll` and `trlvr_launcher.exe`, which together with `openvr_api.dll`, `input/actions.json` (as `trlvr_actions.json`), the `input/bindings_*.json` files and the `openxr_loader.dll` the mingw `d3d9.dll` links against make the payload `frame/install.sh` expects. Upstream's own Windows build (`build.bat`, MSVC) is still in the tree. The design is in [docs/DESIGN.md](docs/DESIGN.md).

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

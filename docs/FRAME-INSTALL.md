# LegendFrame on the Steam Frame: install and tuning guide

This guide is for the Valve Steam Frame (aarch64 SteamOS). It covers installing the Tomb Raider:
Legend VR mod into the Steam copy of the game, what to expect from it, and how to tune it.

The mod is RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port).
LegendFrame is the Steam Frame fork of it. Read the [status](#performance-what-is-measured) section
before you install: the game runs, but it does not yet run at the frame rate the headset can show.

## Requirements

- A Steam Frame with SteamOS and SteamVR, logged into Steam.
- **Tomb Raider: Legend** (Steam app 7000), installed from the Steam store. The installer needs
  `trl.exe` in the Steam library, and the game should have been started once (see
  [first run](#first-run)).
- **Proton 11 (ARM64)** installed in Steam. The installer sets the game and its shortcut to the tool
  named `proton_11-arm64`. Check it under Steam > Settings > Compatibility if the name differs.
- A built or packaged LegendFrame: a folder containing `d3d9.dll`, `d3d9_dxvk.dll`,
  `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json` and the `bindings_*.json` files. Build
  it with the container toolchain in `toolchain/` (see the README) or take a release package.
- A terminal on the Frame (SSH, or Konsole from the desktop).

Why Proton and not a native build: `trl.exe` is a closed-source 32-bit x86 program. In a 32-bit
process under Proton ARM64 every module, the game and the mod's DLLs alike, runs through the FEX
emulator, and there is no ARM64 target for 32-bit Windows code. A native Arm64 port of this game is
not possible. The mod can still be made cheaper to emulate, which is what the stereo rewrite is for.

## What the installer does

`frame/install.sh` is a bash script. It needs no root, can be run again, and refuses to run while the
game is running (`pgrep trl_vr`).

```sh
frame/install.sh                      # payload in ./dist next to the script
frame/install.sh --payload ~/lf-dist  # or anywhere else
```

It does these steps, in this order:

1. **Finds the game.** It reads `~/.local/share/Steam/steamapps/libraryfolders.vdf` and the
   `appmanifest_7000.acf` in each library. `--steam-dir` and `--game-dir` override this.
2. **Backs up** any file it is about to replace that LegendFrame did not put there (for example an
   earlier manual install of the upstream mod) into `<game>/.legendframe-backup-<timestamp>/`.
3. **Copies the payload** into the game folder and records every file in
   `<game>/.legendframe-files.txt`. `openxr_loader.dll` is not copied unless you pass `--with-openxr`
   (SteamVR on the Frame is used through OpenVR; the OpenXR path has not been tried under Proton).
4. **Seeds `trlvr.ini`** only if there is none, with Frame defaults: `fsaa=off`, `msaa=2`,
   `comfort_vignette=off`, `cull_fov_degrees=110`. The mod fills in every other key itself on its first
   run. An existing ini is never touched.
5. **Seeds the game registry** in `compatdata/7000/pfx/user.reg`, section
   `[Software\\Crystal Dynamics\\Tomb Raider: Legend\\Graphics]`: `EnableDepthOfField`,
   `EnableFullscreenEffects`, `EnableVSync`, `EnableShadows`, `EnableReflection` and `EnableWaterFX`
   are set to 0, and `Fullscreen` to 0. This is done only when no Wine process is using that prefix,
   with a copy of `user.reg` saved as `user.reg.legendframe-bak-<timestamp>` first. If the prefix or the
   section does not exist yet, it tells you to run the game once and run the installer again.
6. **Sets SteamVR per-app settings** for app 7000 (`preferredRefreshRate 72`, `resolutionOverride
   1440`) through `/opt/steamvr/bin/linuxarm64/vrcmd`, but only when SteamVR is running. Otherwise it
   prints how to set them by hand. With `--edit-vrsettings` and SteamVR closed, it writes them into
   `~/.config/openvr/config/steamvr.vrsettings` instead (backup kept next to it).
7. **Queues the shortcut.** It copies `frame/shortcut/trlvr-config.py` and `vdf.py` to
   `~/.local/share/legendframe/shortcut/` and writes a drop-in
   `~/.config/systemd/user/steam.service.d/60-legendframe.conf`, then runs
   `systemctl --user daemon-reload`. On Steam's next start, just before Steam comes up, the drop-in
   adds the non-Steam shortcut **Tomb Raider: Legend VR**, maps the shortcut and app 7000 to Proton 11
   ARM64, copies the store art, and deletes itself. **The installer never restarts Steam. Do it
   yourself** (Power > Exit Steam, then start Steam again).

Options: `--userid N` (needed only if more than one Steam account has logged in on the Frame),
`--no-registry`, `--no-vrsettings`, `--no-shortcut`, `--with-openxr`, `--edit-vrsettings`.

### Launch the shortcut, not the store entry

The shortcut is a non-Steam shortcut with the OpenVR flag, so SteamVR treats it as a VR app. It runs
`trlvr_launcher.exe`, which makes a large-address-aware copy of the game (`trl_vr.exe`) and starts it.
The store entry runs the plain `trl.exe`: it starts flat, and because Proton loads a `d3d9.dll`
from the game folder, the mod can still load there, but without the launcher's large-address-aware
copy. Use the shortcut.

### First run

On the first run the game shows its **Setup** dialog (you can run it once from the store entry
before installing, so the registry section exists). In it:

- Turn **Next Generation Content off**. Its separate renderer draws a black scene in VR, and no
  registry value controls it, so the installer cannot do this for you.
- The other options (Depth of Field, Fullscreen Effects, Shadows, Reflections, Water) are set
  by the installer and the mod; the mod overrides Depth of Field and Fullscreen Effects at every start.
- The dialog should not come back unless the graphics adapter changes. If it does, the same answers apply.

## Manual steps, if the installer cannot do them

- **Files:** copy the payload into the game folder
  (`~/.local/share/Steam/steamapps/common/Tomb Raider Legend/`): `d3d9.dll`, `d3d9_dxvk.dll`,
  `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json`, `bindings_*.json`.
- **Shortcut:** in Steam desktop mode, Add a Non-Steam Game, pick `trlvr_launcher.exe` from the game
  folder, set the Start In to the game folder, and in Properties:
  - Launch options:
    `STEAM_COMPAT_DATA_PATH="<library>/steamapps/compatdata/7000" WINEDLLOVERRIDES="d3d9=n,b" DXVK_CONFIG="d3d9.presentInterval = 0" SteamAppId=7000 %command%`
  - Compatibility: force Proton 11 (ARM64).
  - The VR flag (OpenVR) is not exposed in the UI; the script sets it. If you can, use the script:
    with Steam closed, `python3 -I ~/.local/share/legendframe/shortcut/trlvr-config.py --game-dir <game folder>`.
- **Proton for the store entry:** Steam > Tomb Raider: Legend > Properties > Compatibility, force
  Proton 11 (ARM64), so that the shortcut and the store entry share one prefix (`compatdata/7000`).
- **Per-app SteamVR settings:** SteamVR Settings > Video > per-application: Tomb Raider: Legend, refresh
  rate 72 Hz, render resolution 1440. Or with SteamVR running:
  `/opt/steamvr/bin/linuxarm64/vrcmd --set-settings-int steam.app.7000.preferredRefreshRate 72` and
  `... steam.app.7000.resolutionOverride 1440`. The mod reads the resolution at start-up, so restart
  the game after changing it.
- **Registry:** with every Wine process stopped, edit `user.reg` as described above, or set the
  options in the game's Setup dialog.

## Performance: what is measured

Measured on one Steam Frame with the upstream 1.0.0 mod, which is what current LegendFrame builds
behave like:

| Setup | Result |
|---|---|
| Two-pass stereo (the mod's normal mode), 1728 px per eye | gameplay median about 39 fps |
| Same, 1440 px per eye at 72 Hz, shadows, reflections and water off | median about 40 fps, 2000 draws per frame |
| `stereo_same_frame=0` (alternate-eye) | median 68 fps, but see below |

What this tells us:

- The game is **not** limited by resolution: going from 1728 to 1440 changed nothing, and cutting
  draw calls by a quarter (shadows, reflections, water off) changed the GPU time very little.
- The GPU is mostly waiting. The CPU side is the limit. Each D3D9 draw goes through the emulated game,
  the emulated D3D9 front end of DXVK and the Vulkan translation layers, and the mod issues every draw
  **twice**, once per eye, into one double-wide target. About 2000 draws a frame, doubled, is more
  submission work than the emulated CPU can do in 13.9 ms (72 Hz) or 11.1 ms (90 Hz).
- Alternate-eye rendering (one eye per frame) confirmed this: the same scene reached 68 fps because
  each frame submitted half the draws. It is not usable, though: it shows doubled images and is
  nauseating. `stereo_same_frame` should stay `1`.
- Heat matters. When the Frame was already hot from earlier work, the same scene fell to about 31 fps
  and the fan logged high temperatures. Let the headset cool before you compare numbers.
- FEX is already configured for the game (x87 reduced precision, TSO and multiblock on) in Proton's
  `Config.json`. Do not set `FEX_SMCCHECKS=none`: the mod patches game code at run time. Turning TSO off
  is risky. Other FEX knobs (`FEX_MAXINST`, `FEX_PROFILESTATS=0`) have not been tried.

**The rewrite goal:** issue each draw once and render both eyes from it, with Vulkan multiview in the
DXVK fork (a per-view clip transform in the generated vertex shaders, one draw for both eyes), keeping
the two-pass path as a fallback. That halves the submission work without changing the picture. This
is in development; until it lands, expect about 40 fps in busy scenes and a SteamVR that holds the
rest with throttling or motion smoothing.

## Tuning

Edit `trlvr.ini` in the game folder with the game closed. The file explains every key. The ones that
matter on the Frame:

| Key | Frame default | Notes |
|---|---|---|
| `stereo_same_frame` | `1` | Both eyes in one frame. `0` is alternate-eye: faster, but unusable. |
| `fsaa` | `off` | The game's anti-aliasing is expensive. |
| `msaa` | `2` | Used only when `fsaa` is on. |
| `comfort_vignette` | `off` | `jumps` or `full` darken the edge of view while moving. |
| `cull_fov_degrees` | `110` | Objects outside this angle are not drawn. Lower is cheaper but may pop. |
| `first_person_object_culling`, `terrain_culling` | `frustum` | Keep at `frustum`; `open` is slow. |
| `ui_scale`, `hud_follow` | `0.6`, `0.85` | HUD size and how it follows your head. |
| `vr_runtime` | `auto` | `openvr` forces SteamVR's OpenVR path. |

Outside the ini: the per-app SteamVR resolution and refresh rate (the largest knobs), and the game's
Setup dialog for Shadows, Reflections and Water (the installer turns them off in the registry).
Resolution did not change the frame rate in testing, but may matter once the draw cost falls.

## Controls

The bindings ship as `bindings_*.json`; SteamVR remaps the Oculus Touch binding onto the Frame
controllers. The upstream layout is in
[`release/VR CONTROLLER BINDINGS.txt`](../release/VR%20CONTROLLER%20BINDINGS.txt). On the Frame the
**left menu button** is the controller's `view` input mapped onto the Touch menu button: a tap is
meant to pause and a long press to bring up the in-game menu. The left system button always opens the SteamVR dashboard and is never
bound. If a button does nothing, open SteamVR > Settings > Controllers > Manage Controller Bindings and
reset the choice for Tomb Raider: Legend.

## Logs to collect when something goes wrong

All are in the game folder:

- `trlvr.log` (and `trlvr.log.prev`): the headset, hooks, controllers, and a `perf:` line every five seconds
  with frame rate, GPU and CPU time, draws and throttling.
- `trlvr_launcher.log` and `trl_vr_d3d9.log`: the launcher and the DXVK side.
- `trlvr_crash.dmp` (and `.prev`) after a crash.
- SteamVR's `vrserver.txt` and `vrcompositor.txt` under `~/.local/share/Steam/logs/`.

Pull out the `perf:` lines first: they say whether a slow area is CPU- or GPU-bound. Look through them
for anything personal before sharing a log.

## Uninstall

```sh
frame/uninstall.sh
```

It refuses to run while the game runs. It deletes the files listed in `.legendframe-files.txt`,
the files the mod generated (`trl_vr.exe`, logs, crash dumps), `trlvr.ini` if the installer created it,
restores your backups from `.legendframe-backup-*`, and queues removal of the shortcut as a
drop-in (`61-legendframe-remove.conf`) for the next Steam start. It does not restart Steam.

It leaves the registry values, the SteamVR per-app settings and the Proton mapping for app 7000 alone
and prints how to revert each. The game's own files are never modified, so Steam's
"Verify integrity" never needs to repair anything.

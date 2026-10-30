# LegendFrame on the Steam Frame: install and tuning guide

This guide is for the Valve Steam Frame (aarch64 SteamOS). It covers installing the Tomb Raider:
Legend VR mod into the Steam copy of the game, what to expect from it, and how to tune it.

The mod is RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port).
LegendFrame is the Steam Frame fork of it. This guide describes LegendFrame 1.0.0, which is upstream mod 1.1.0 plus the Frame work. The
[performance](#performance-what-is-measured) section has the measurements behind it.

## Requirements

- A Steam Frame with SteamOS and SteamVR, logged into Steam.
- **Tomb Raider: Legend** (Steam app 7000), installed from the Steam store. The installer needs
  `trl.exe` in the Steam library, and the game should have been started once (see
  [first run](#first-run)).
- **Proton 11 (ARM64)** installed in Steam. The installer sets the game and its shortcut to the tool
  named `proton_11-arm64`. Check it under Steam > Settings > Compatibility if the name differs.
- A built or packaged LegendFrame: a folder containing `d3d9.dll`, `d3d9_dxvk.dll`,
  `openvr_api.dll`, `trlvr_launcher.exe`, `trlvr_actions.json` and the `bindings_*.json` files. Build
  it with the container toolchain in `toolchain/` (see the README; it runs on the Frame itself) or take a
  release package. There is no GitHub release yet. The mingw-built `d3d9.dll` links
  `openxr_loader.dll` statically, so that file must be in the payload and be installed next to it.
- A terminal on the Frame (SSH, or Konsole from the desktop).

Why Proton and not a native build: `trl.exe` is a closed-source 32-bit x86 program. In a 32-bit
process under Proton ARM64 every module, the game and the mod's DLLs alike, runs through the FEX
emulator, and there is no ARM64 target for 32-bit Windows code. A native Arm64 port of this game is
not possible. The mod can still be made cheaper to emulate, which is what the single-pass stereo
work is for.

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
   `<game>/.legendframe-files.txt`. The payload must contain `openvr_api.dll`; it is not built here,
   so copy it from the upstream TRLVR release zip into the payload folder (the installer stops with that
   message if it is missing). `openxr_loader.dll` is copied by default, because the mingw-built
   `d3d9.dll` imports it statically and does not load without it, even though the Frame runs through
   OpenVR (see [runtimes](#openvr-and-openxr)); `--without-openxr` leaves it out. It also writes
   `<game>/.legendframe-version` with the first line of the payload's `VERSION` file (`unknown` if
   there is none); `frame/install.sh --check` prints it and changes nothing.
4. **Seeds `trlvr.ini`** with Frame defaults, adding only keys that are absent under `[vr]`. A new
   file gets all of them; an existing one keeps every key and line it has (CRLF line endings and a BOM
   included), gains the missing ones, and the installer prints which keys it added and which it kept.
   The keys are `fsaa=off`, `msaa=2`, `comfort_vignette=off`, `cull_fov_degrees=110`,
   `stereo_mode=singlepass`, `stereo_stats=0`, `vr_runtime=openvr`, `submit_wait=0`,
   `god_carry_fix=1`, `hud_offset_x=-0.08`, `hud_offset_y=0.08`, `hud_items_x=0.20`, `hud_items_y=0.20`,
   `hud_ammo_x=0.20`, `hud_ammo_y=-0.20`, `overlay_vr_width_degrees=50`, `overlay_vr_resolution=1536`
   and `overlay_vr_text_scale=1.25`. One existing value is moved on: `overlay_vr_width_degrees=38`, the
   old default, becomes `50`. The mod fills in every other key itself on its first run.
5. **Seeds the game registry** in `compatdata/7000/pfx/user.reg`, section
   `[Software\\Crystal Dynamics\\Tomb Raider: Legend\\Graphics]`: `EnableDepthOfField`,
   `EnableFullscreenEffects`, `EnableVSync`, `EnableShadows`, `EnableReflection`, `EnableWaterFX` and
   `Fullscreen`, as 0. Only values that are absent are added; a value that already exists (for example
   `Fullscreen=1`) is left alone and listed. `--force-registry` overwrites existing values too. This
   is done only when no Wine process is using that prefix, with a copy of `user.reg` saved as
   `user.reg.legendframe-bak-<timestamp>` first. If the prefix or the section does not exist yet, it
   tells you to run the game once and run the installer again.
6. **Sets SteamVR per-app settings** for app 7000 (`preferredRefreshRate 72`, `resolutionOverride
   1440`) through `/opt/steamvr/bin/linuxarm64/vrcmd`, but only when SteamVR is running, and only
   values that are absent: it reads each one first (`vrcmd --settings-int`), keeps and prints an
   existing value (for example a 1728 resolution override), and writes only what is missing.
   `--vrsettings "refresh=72,res=1440"` forces those values over existing ones (either part may be
   given alone); `--no-vrsettings` skips the step. When SteamVR is not running it prints how to set
   them by hand. With `--edit-vrsettings` and SteamVR closed, it writes the missing values into
   `~/.config/openvr/config/steamvr.vrsettings` instead (backup kept next to it), with the same
   keep-what-exists rule.
7. **Queues the shortcut.** It copies `frame/shortcut/trlvr-config.py` and `vdf.py` to
   `~/.local/share/legendframe/shortcut/` and writes a drop-in
   `~/.config/systemd/user/steam.service.d/60-legendframe.conf`, then runs
   `systemctl --user daemon-reload`. On Steam's next start, just before Steam comes up, the drop-in
   adds the non-Steam shortcut **Tomb Raider: Legend VR**, maps the shortcut and app 7000 to Proton 11
   ARM64, copies the store art, and deletes itself. **The installer never restarts Steam. Do it
   yourself** (Power > Exit Steam, then start Steam again).

Options: `--userid N` (needed only if more than one Steam account has logged in on the Frame),
`--without-openxr`, `--vrsettings "refresh=N,res=N"`, `--edit-vrsettings`, `--force-registry`,
`--no-registry`, `--no-vrsettings`, `--no-shortcut`, `--check`.

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

All numbers are from one Steam Frame at 72 Hz, 1440 px per eye, Proton 11 ARM64, OpenVR, shadows,
reflections and water off. The owner's own play sessions supplied the gameplay figures.

| Setup | Result | Status |
|---|---|---|
| Upstream 1.0.0, two-pass stereo, 1728 px per eye | gameplay median about 39 fps | measured |
| Upstream two-pass, 1440 px per eye | gameplay median about 40 fps, 2000 draws per frame | measured |
| `stereo_same_frame=0` (alternate-eye) | median 68 fps, but unusable (doubled images) | measured |
| LegendFrame 0.1.1, single-pass | gameplay median about 51 fps (worst 27 to 39, best 72), frame about 19.8 ms, about 1450 draws, fallback and rejected draws 0 | measured |
| Menu, `submit_wait=1` (GPU wait kept) | 43 to 45 fps, 23 ms frame, 9 ms at Present | measured |
| Menu, `submit_wait=0` (no GPU wait) | 67 to 70 fps, 14.5 ms frame, 1 ms at Present | measured, menu only |
| LegendFrame 1.0.0, single-pass, `submit_wait=0` | gameplay median 72 fps (the refresh cap), minimum 66, frame about 13.9 ms, fallback and rejected draws 0, no tearing | measured |
| God-mode carries with `god_carry_fix=1` | carries before and after cutscenes and with Lara wet stay clean | measured |

What this tells us:

- The game is **not** limited by resolution or by the GPU. Going from 1728 to 1440 px changed
  nothing, and cutting a quarter of the draws changed GPU time very little.
- The CPU side is the limit. Each D3D9 draw goes through the emulated game, the emulated D3D9 front
  end of DXVK and the Vulkan translation layers. Upstream issues every draw **twice**, once per eye.
  Alternate-eye rendering (half the draws per frame) reached 68 fps and confirmed it.
- **Single-pass stereo** is the answer to that: one D3D9 draw and one `vkCmdDraw` for both eyes. It is
  not Vulkan multiview. The draw is issued with `instanceCount` doubled, the vertex shader epilogue sets
  `ViewportIndex = InstanceIndex & 1`, two viewports cover the two halves of the existing double-wide
  target, and `c0..c3` come from a left/right bank in the vertex-shader constants. The bank is what the
  two-pass path already computed, so the images match two-pass (the headless harness checks this with
  a maximum difference of 0). Draws it cannot take (instanced, fixed-function, per-eye textures,
  reflection pass) fall back to two-pass per draw. Design: [DESIGN.md](DESIGN.md).
- The proxy was first built with x87 maths (the mingw i686 default) and is now built with SSE2, which
  matters under FEX. The per-eye matrix work is also done for both eyes in one call
  (`stereo_shared_math`, bit-exact against two calls on 8460 synthetic and 13207 captured sets), at
  about 47 percent fewer instructions per stereo draw.
- `submit_wait=0` skips the CPU wait for the GPU before the OpenVR `Submit`. The order is still
  guaranteed by the submission-queue lock and the recorded layout barrier (the same pattern DXVK's own
  D3D11 OpenVR interop uses), and no tearing showed in play. Menu
  gain was large (2.4 to 3 ms per frame was the estimate; the menu A/B above is the measurement).
- Heat matters a lot. A 0.1.2 session read a 36 fps median that was the Frame throttling (many fan-control
  temperature warnings) while another job had just run the headset hard; the same code gave 51 fps cool.
  Let the headset cool before you compare numbers, and run nothing else on it while testing.
- FEX is already configured for the game (x87 reduced precision, TSO and multiblock on) in Proton's
  `Config.json`. Do not set `FEX_SMCCHECKS=none`: the mod patches game code at run time. Turning TSO off
  is risky. Other FEX knobs (`FEX_MAXINST`, `FEX_PROFILESTATS=0`) have not been tried.

Next levers, not done: the remaining per-draw hook overhead (other-instance draws about 2 to 3 ms,
terrain about 1 ms in the `perf cpu:` lines), and fewer constant-change arm calls.

### OpenVR and OpenXR

The Frame default is `vr_runtime=openvr`. OpenXR is **not available under Proton 11 ARM64** for this
mod: `xrCreateInstance` returns `XR_ERROR_RUNTIME_UNAVAILABLE` (there is no 32-bit OpenXR runtime
bridge there), and forcing `vr_runtime=openxr` leaves the game running flat. `auto` wastes several
seconds before falling back to OpenVR. Under **Proton Experimental (ARM64)** OpenXR works (the session
came up at 1440 per eye with the stereo stats fine), but its menu frame rate was lower (32 to 40 fps
against 42 on Proton 11, two runs each). It is untested in gameplay; to try it, set the shortcut's
compatibility tool to Proton Experimental and `vr_runtime=openxr`.

## Tuning

Edit `trlvr.ini` in the game folder with the game closed. The file explains every key. The ones that
matter on the Frame (section `[vr]`):

| Key | Code default | Frame setting | Notes |
|---|---|---|---|
| `stereo_mode` | `singlepass` | `singlepass` | `singlepass` issues one draw for both eyes; `twopass` is upstream's path and the fallback. The mode can also be flipped live with **Shift+F6** or the settings-panel checkbox (not saved). |
| `stereo_stats` | `0` | `0` | Logs a `stereo stats:` line every 5 s: single-pass and fallback draw counts and timing. Cheap. |
| `stereo_shared_math` | `1` | `1` | Both eyes' matrices in one call (bit-exact). `0` gives the two legacy calls, for A/B. |
| `submit_wait` | `0` | `0` | `0` = no CPU wait for the GPU before `Submit`. `1` is the 1.1.0 behaviour (slower). OpenXR path is unaffected. |
| `god_carry_fix` | `1` | `1` | Fix for upstream god-mode carry smearing when Lara is drawn with a second model. `0` is the 1.1.0 carry, for A/B. |
| `hud_offset_x`, `hud_offset_y` | `0`, `0` | `-0.08`, `0.08` | Nudges the gameplay HUD only (NDC units). Also sliders in the settings panel. |
| `hud_items_x`, `hud_items_y` | `0`, `0` | `0.20`, `0.20` | Moves the whole item wheel (d-pad gear cross, every slot) inside the game's interface, same units as `hud_offset`. `hud_items_hide` (`0`) hides it. Also sliders in the settings panel; the gameplay HUD stays in view while the panel is open. |
| `hud_ammo_x`, `hud_ammo_y` | `0`, `0` | `0.20`, `-0.20` | The same for the weapon and ammo readout (icon and numbers), with `hud_ammo_hide`. |
| `hud_debug` | `0` | `0` | Logs the player HUD's item tree (fade groups, anchor, which element each part belongs to) and the HUD screens to `trlvr.log`. |
| `overlay_vr_width_degrees` | `50` | `50` | Settings panel width in the headset. 1.1.0 used 38 (too small to read); the installer moves an unchanged 38 to 50. Also a panel slider. |
| `overlay_vr_text_scale` | `1.25` | `1.25` | Panel text and control size; 1 = the 1.1.0 layout (768 px of content). Larger fits less at once. Panel slider; applies when the panel next opens. |
| `overlay_vr_resolution` | `1536` | `1536` | Panel texture size, square, 512 to 4096, with mipmaps. About 12 MB of video memory at 1536, made the first time the panel opens. |
| `vr_runtime` | `auto` | `openvr` | `openxr` does not work under Proton 11 ARM64; see [above](#openvr-and-openxr). |
| `vr_capture_c0` | `0` | `0` | Debug: records the game's `c0` and state for the maths test. Leave off. |
| `stereo_same_frame` | `1` | `1` | Both eyes in one frame. `0` is alternate-eye: faster, but unusable. |
| `fsaa` | `off` | `off` | The game's anti-aliasing is expensive. |
| `msaa` | `2` | `2` | Used only when `fsaa` is on. |
| `comfort_vignette` | | `off` | `jumps` or `full` darken the edge of view while moving. |
| `cull_fov_degrees` | | `110` | Objects outside this angle are not drawn. Lower is cheaper but may pop. |
| `first_person_object_culling`, `terrain_culling` | | `frustum` | Keep at `frustum`; `open` is slow. |
| `ui_scale`, `hud_follow` | | `0.6`, `0.85` | HUD size and how it follows your head. |

The "Frame setting" column is what `frame/install.sh` seeds. The installer adds any of the Frame keys
that are missing (see step 4 above) and changes none that exist, except an unchanged
`overlay_vr_width_degrees=38`. Outside the ini: the per-app
SteamVR resolution and refresh rate, and the game's Setup dialog for Shadows, Reflections and Water (the
installer turns them off in the registry when those values are absent).

## Controls

The bindings ship as `bindings_*.json`; SteamVR remaps the Oculus Touch binding onto the Frame
controllers. The upstream layout is in
[`release/VR CONTROLLER BINDINGS.txt`](../release/VR%20CONTROLLER%20BINDINGS.txt). On the Frame the
**left menu button** is the controller's own menu button, with a native Frame binding: a tap opens the
gear menu and pauses, and holding it for about half a second opens the settings panel. The left system button always opens the SteamVR dashboard and is never
bound. If a button does nothing, open SteamVR > Settings > Controllers > Manage Controller Bindings and
reset the choice for Tomb Raider: Legend.

## Logs to collect when something goes wrong

All are in the game folder:

- `trlvr.log` (and `trlvr.log.prev`): the headset, hooks, controllers, and a `perf:` line every five seconds
  with frame rate, GPU and CPU time, draws and throttling. Lines specific to LegendFrame:
  - `stereo: single-pass (viewport index) enabled, DXVK stereo interface v1`: the single-pass path is up.
    Bad signs instead: `stereo: DXVK has no single-pass interface`, `stereo draw rejected`,
    `stereo lowering failed`, `EnableSinglePassStereo failed` (the game then runs two-pass).
  - `stereo: dual-eye substitution on (...)`: shared per-eye maths is on.
  - `stereo: live toggle -> single-pass|two-pass (Shift+F6)`: you switched the mode.
  - `stereo stats: single N, fallback N (...), mono N, arm calls N, dxvk rejected N` every 5 s with
    `stereo_stats=1`, ending in `arm X ms/frame (N calls, M skipped), begin X ms/frame`. In gameplay
    `single` should be 90 percent or more of the stereo draws.
  - `submit: GPU wait before Submit off (submit_wait=0)` (or `on (submit_wait=1)`), once at start-up.
  - `carry #N: started (god_carry_fix 1|0)` and `carry #N: ended -- ...`, one pair per god-mode carry,
    plus lines about a second model drawn on her instance; a line ending
    `SOME OF ITS VERTICES STAY AT THE PICK-UP SPOT` marks the smear.
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

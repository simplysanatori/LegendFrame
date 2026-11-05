# LegendFrame user guide

The full reference for playing LegendFrame on the Steam Frame: install detail, controls, settings, performance, files and troubleshooting. The short version is the [README](../README.md).

## Known issues

- The game window starts docked in the SteamVR dashboard and hides with it. SteamVR's "Auto show game theater" setting sends every new game window to the theater screen, so the Frame install turns that setting off. If I still see a floating window, I press "Connect to dock" on it once, or turn off "Auto show game theater" under SteamVR Settings > Dashboard (Advanced). The setting also stops flat apps opening on the theater screen by themselves.
- The game runs through OpenVR, not OpenXR. OpenXR isn't available to 32-bit games under Proton 11 ARM64.
- Turn Next Generation Content off in the game's Setup dialog, or the VR view is black.
- Very busy areas can still dip below 72. The game's own logic runs through the emulator.

## Requirements

- A Steam Frame with SteamOS and SteamVR, logged into Steam.
- **Tomb Raider: Legend** (Steam app 7000), installed from the Steam store. Start it once from the store entry so its Wine prefix and Setup values exist.
- **Proton 11 (ARM64)** installed in Steam. The installer sets the game and its shortcut to the tool named `proton_11-arm64`. Check it under Steam > Settings > Compatibility if the name differs.
- `legendframe-1.0.1-steam-frame-linux-arm64.zip` from the [Releases](https://github.com/simplysanatori/LegendFrame/releases) page.
- A terminal on the Frame: SSH, or Konsole from desktop mode.

You need your own copy of Tomb Raider: Legend from Steam. No game data is in this repository.

## Install

The steps are in the [README](../README.md#install). More on each one:

- **Install** (`frame/install.sh --payload ~/Downloads/legendframe-1.0.1-steam-frame-linux-arm64.zip`). The installer finds the game, backs up any mod files already there and copies the mod in. It seeds the Frame settings in `trlvr.ini` and the game's graphics registry values. It sets SteamVR's per-app settings (72 Hz, resolution 1440) when SteamVR is running. It queues the **Tomb Raider: Legend VR** shortcut for Steam's next start. Values you already have are kept. It needs no root and can be run again. `frame/install.sh --help` lists the options. `frame/install.sh --check` prints the installed version.
- **Restart Steam** yourself (Power > Exit Steam, then start Steam again). The installer never does it. On that start Steam gets the shortcut **Tomb Raider: Legend VR**, set to Proton 11 ARM64, with the store art.
- **Launch** **Tomb Raider: Legend VR**, not the store entry. The shortcut starts the game as a VR app through `trlvr_launcher.exe`. The store entry starts it flat.

[FRAME-INSTALL.md](FRAME-INSTALL.md) has what each installer step does, every option, and the manual steps if the installer cannot do one.

### Install with Frametanium

If you use [Frametanium](https://github.com/Cogential/frametanium), add `simplysanatori/LegendFrame` there. It downloads the release, runs its `install.sh` and updates it when I publish a new version. It uses the `legendframe-<version>-steam-frame-linux-arm64.zip` asset, which holds the mod files and the installer, and it does what `frame/install.sh --payload` does: install the mod into the Steam copy of Tomb Raider: Legend, seed the Frame settings, and queue the shortcut. Install Tomb Raider: Legend from Steam first and run it once from the store entry. If the shortcut is new, restart Steam yourself afterwards; the installer never does.

### Install with FrameDrop

[FrameDrop](https://framedropvr.com) can install this too: [Install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FLegendFrame%2Freleases%2Flatest%2Fdownload%2Flegendframe.framedrop.json). The release has a `legendframe-<version>-linux-arm64-framedrop.zip` and a `legendframe.framedrop.json` for it. FrameDrop unpacks the zip on the Frame and adds its own library entry, which runs the `run.sh` in the zip. The first time it runs, and again after an update, `run.sh` runs `install.sh` with the Steam shortcut step turned off, so you get FrameDrop's entry and no second one from the installer, and then tells you where to start the game. Install Tomb Raider: Legend from Steam first. The mod runs under Proton, so there is nothing for FrameDrop's entry to start: `run.sh` puts the mod into the Steam copy of the game, without the shortcut step, and tells you to start the game from the "Tomb Raider: Legend VR" entry. Run `install.sh` from the unpacked folder once to get that entry if you do not have it.

## Update and uninstall

To update, run the installer again with the new zip. To remove LegendFrame, run `frame/uninstall.sh`. It deletes the mod's files, restores your backups and queues removal of the shortcut for the next Steam start. It leaves the registry values, the SteamVR per-app settings and the Proton mapping for app 7000 alone, and prints how to revert each.

## First run

On the first run the game shows its **Setup** dialog. You can run it once from the store entry before installing, so the registry section exists. In it:

- Turn **Next Generation Content off**. Its separate renderer draws a black scene in VR, and no registry value controls it, so the installer cannot do this for you.
- The other options (Depth of Field, Fullscreen Effects, Shadows, Reflections, Water) are set by the installer and the mod. The mod overrides Depth of Field and Fullscreen Effects at every start.
- The dialog should not come back unless the graphics adapter changes. If it does, the same answers apply.

If the registry section did not exist when you installed, the installer says so: run the game once and run the installer again.

## Controls

The Frame controllers have their own layout. The upstream one was made for Oculus Touch: on the Frame, X and Y did the same thing as B, three of the d-pad buttons threw grenades, the right Menu button did nothing and the prompts named buttons the Frame doesn't have. Now every button has its own job:

| Frame control | In play | In menus |
|---|---|---|
| Left stick | Move. Double click recentres the view. Hold it in and push it for the radial gear menu | Move |
| Right stick | Turn. Click switches weapons, double click opens the PDA, long press uses the binoculars (with immersive controls it switches between first and third person) | Scroll |
| A | Jump / surface | Select |
| B | Crouch / roll / dive | Back |
| X | Precision aim on / off. In third person (classic, shoulder or board view) I press X while Lara is on her feet, and X again leaves it. It does nothing while she jumps, hangs or climbs. In the binocular view X puts the binoculars away first. In immersive first person with the guns away it draws the pistols | |
| Y | Grenade / flare. A long press is the personal light with immersive controls off | |
| D-pad up | Medipack | Up |
| D-pad right | Binoculars. Press it again to put them away | Right |
| D-pad down | Switch weapons | Down |
| D-pad left | Personal light | Left |
| Menu (right) | Tap to pause. Hold it 0.6 s for the settings panel | Back |
| View (left) | PDA | |
| Left trigger | Action / interact / pull the grapple | |
| Right trigger | Fire. In immersive first person with the guns away it draws the pistols | Select |
| Left grip | Throw the grapple | |
| Right grip | Lock on | |
| Left bumper | Hold it for the board-mode finger flick | |
| Right bumper | In immersive first person, hold it (0.3 s) to holster whatever is drawn: the pistols go to the hips, the long gun to the shoulder | |
| Steam buttons | SteamVR's (dashboard, recentre). Never bound | |

The d-pad is the game's own gear cross, the same four items in the same places, and the item wheel flashes up when I press it. It works in every mode. D-pad down is the game's weapon select in first person as well: it steps to the other gun Lara carries, and when she only has the pistols the game shows its weapon selector. The holsters follow whatever the game did, so a hip or shoulder grip still puts the gun away. In immersive first person d-pad right raises the binoculars through the same hand state as the belt. While the binoculars are up, d-pad right puts them away, d-pad up still uses a medipack, and down and left wait. The left stick's radial gear menu still works as before.

Quick draw: in immersive first person, with the guns holstered and nothing in my hands, a pull of the fire trigger (or a press of X) draws the pistols straight away, the same draw as reaching to the hips. The pull that draws doesn't fire, the next one does, and a hip grip holsters them as usual. To put the guns away I hold the right bumper for a moment: the pistols go back to the hips and the long gun to the shoulder, the same as reaching there, so the trigger draws and the bumper holsters. The Frame's cameras often lose the controllers at the hips and behind the shoulder, which makes the button the dependable way, but the hip and shoulder reaches still holster too. With a gun out, X is precision aim, like the left trigger. `frame_quick_draw = 0` in the `[controls]` section of `trlvr.ini` turns the quick draw off. In third person X's precision aim draws the guns and aims where I look. The game only lets Lara aim precisely on her feet: mid-jump, hanging or climbing the press does nothing, and a hint in the game's own hint box says why, the way its tutorial hints do. From the binocular view X puts the binoculars away and then aims. In first person the guns aim where my hand points, and `controller_aim = 0` makes them follow the headset instead.

The in-game prompts name the Frame's buttons (A, B, X, Y, D-pad Up and so on, Menu, View), in English. Other languages keep upstream's names for now. In immersive first person Y gives way to the chest pouch, as upstream.

Other controllers keep upstream's layout: see [VR CONTROLLER BINDINGS.txt](../release/VR%20CONTROLLER%20BINDINGS.txt).

If a button does nothing, or does what it did before this layout, SteamVR is using an older binding it kept for the game. Open SteamVR > Settings > Controllers > Manage Controller Bindings, pick Tomb Raider: Legend and select the supplied LegendFrame binding again.

## Settings

Edit `trlvr.ini` in the game folder with the game closed. The mod writes every key with its explanation the first time it runs. The settings panel (hold the Menu button) changes most of them live. The ones that matter on the Frame, with the values a Frame install starts with:

| Key | Default | What it does |
|---|---|---|
| `stereo_mode` | `singlepass` | `singlepass` sends each draw once for both eyes. `twopass` is the original mod's renderer. The settings panel and Shift+F6 switch it live |
| `submit_wait` | `0` | `1` makes the CPU wait for the GPU before each frame goes to SteamVR, as the original mod did. Slower |
| `god_carry_fix` | `1` | Keeps carried characters whole in god mode. `0` is the original carry |
| `vr_runtime` | `openvr` | The installer sets `openvr`. `openxr` leaves the game flat under Proton 11 ARM64 |
| `hud_offset_x`, `hud_offset_y` | `-0.08`, `0.08` | Moves the whole gameplay HUD. The Whole HUD sliders in the settings panel |
| `hud_items_x`, `hud_items_y` | `0.20`, `0.20` | Moves the whole item wheel (the d-pad gear cross, every slot), in the same units as `hud_offset`. Sliders in the settings panel, where the HUD stays in view while you move them. `hud_items_hide` (`0`) hides it |
| `hud_ammo_x`, `hud_ammo_y` | `0.20`, `-0.20` | Moves the weapon and ammo readout, icon and numbers together, the same way. `hud_ammo_hide` (`0`) hides it |
| `hud_health_x`, `hud_health_y` | `0.20`, `-0.20` | Moves the health bar, with the breath bar above it, the same way. `hud_health_hide` (`0`) hides it. Reset to defaults in the settings panel puts these three and `hud_offset` back to the values in this table |
| `frame_quick_draw` | `1` | In the `[controls]` section. With the guns holstered in immersive first person, the fire trigger or X draws the pistols straight away. `0` turns that off |
| `frame_holster_hold` | `0.3` | In the `[controls]` section. How long, in seconds, the right bumper is held to holster the guns in immersive first person. `0` holsters on a tap |
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

[FRAME-INSTALL.md](FRAME-INSTALL.md#tuning) has every key, with the code default next to the Frame setting.

## Performance

On one Steam Frame at 72 Hz, 1440 pixels per eye, Proton 11 ARM64 and OpenVR, with shadows, reflections and water off:

| | Original mod | LegendFrame |
|---|---|---|
| Gameplay frame rate | about 40 fps | 72 fps most of the time, dips into the 50s in the busiest areas |
| Time per frame | about 25 ms | about 14 ms |

Let the headset cool between long sessions: a hot Frame throttles and drops the frame rate.

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
- **A floating 2D window covers the VR view.** That is the game window. The Frame install turns off SteamVR's "Auto show game theater", which is what sends a new game window to the theater screen. If the window still floats, press "Connect to dock" on it once, or turn off "Auto show game theater" under SteamVR Settings > Dashboard (Advanced). With the SteamVR server running, `/opt/steamvr/bin/linuxarm64/vrcmd --set-settings-bool dashboard.autoShowGameTheater false` does the same. The setting also stops flat apps opening on the theater screen by themselves; the same command with `true` brings that back.
- **The scene is black in VR.** Turn **Next Generation Content off** in the game's Setup dialog.
- **A controller button does nothing, or still does what it did in an older version.** Select the supplied binding again in SteamVR > Settings > Controllers > Manage Controller Bindings for Tomb Raider: Legend. The left system button always opens the SteamVR dashboard.
- **The installer says the registry section does not exist.** Run the game once from the store entry, then run the installer again.
- **Single-pass is not working.** `trlvr.log` has `stereo: single-pass (viewport index) enabled, DXVK stereo interface v1` when it is up. If it says `stereo: DXVK has no single-pass interface` or `EnableSinglePassStereo failed` instead, the game runs two-pass: reinstall so `d3d9.dll` and `d3d9_dxvk.dll` come from the same release.
- **Frames look torn or incomplete.** Set `submit_wait=1` and tell me what you saw.
- **Frame rate is lower than expected.** Look at the `perf:` lines in `trlvr.log`: they say whether a slow area is CPU- or GPU-bound. Let the headset cool, and run nothing else on it.
- **Roll back.** Run `frame/uninstall.sh`. Steam's "Verify integrity" also works, as the game's own files are never changed.

## Logs

Open an [issue](https://github.com/simplysanatori/LegendFrame/issues) with what you did, what you expected and what happened, and the logs. They are all in the game folder:

- `trlvr.log` (and `trlvr.log.prev`): the headset, hooks, controllers, and a `perf:` line every five seconds with frame rate, GPU and CPU time, draws and throttling. Pull out the `perf:` lines first.
- `trlvr_launcher.log` and `trl_vr_d3d9.log`: the launcher and the DXVK side.
- `trlvr_crash.dmp` (and `.prev`) after a crash.
- SteamVR's `vrserver.txt` and `vrcompositor.txt` under `~/.local/share/Steam/logs/`.

Look through them for anything personal before you share them. [FRAME-INSTALL.md](FRAME-INSTALL.md#logs-to-collect-when-something-goes-wrong) lists the LegendFrame-specific log lines.

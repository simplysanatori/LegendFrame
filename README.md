# LegendFrame

Tomb Raider: Legend in VR on the Valve Steam Frame.

LegendFrame is a Steam Frame fork of **[Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port)** by **RototRobot**: a virtual reality mod for Tomb Raider: Legend with stereoscopic rendering, 6DoF head tracking, motion controllers, a VR third-person camera and an optional first-person mode. All of the VR work is theirs. LegendFrame adapts it to run on the Frame, which is an Arm64 Linux headset, through Proton 11 ARM64 and SteamVR. The original README is kept in [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Apache License 2.0, as upstream.

You need your own copy of Tomb Raider: Legend from Steam. No game data is in this repository.

## Status

Work in progress; the single-pass stereo rewrite is in development; current builds behave like upstream 1.0.0 on the Frame.

On the Frame the mod runs and is playable, but slowly: about 40 fps in gameplay on a 72 Hz display, measured with the upstream mod. The game is a 32-bit x86 program, so the whole thing is emulated, and the mod submits every draw twice, once per eye. That submission cost, not the GPU, is the limit. The rewrite (one draw for both eyes with Vulkan multiview) targets it directly. The measurements are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md#performance-what-is-measured).

## Install on the Frame

You need a Steam Frame with Tomb Raider: Legend installed from Steam, Proton 11 ARM64, and a built payload (see Building).

```sh
git clone https://github.com/simplysanatori/LegendFrame.git
cd LegendFrame
git checkout steam-frame
frame/install.sh --payload /path/to/dist
```

The installer finds the game, backs up any mod files already there, copies the payload, seeds Frame defaults for `trlvr.ini` and the game's graphics registry values, sets SteamVR's per-app settings (72 Hz, resolution 1440) when SteamVR is running, and queues the **Tomb Raider: Legend VR** shortcut for Steam's next start. It does not restart Steam. Restart it yourself, then launch the shortcut, not the store entry. `frame/uninstall.sh` reverses it.

[docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md) has the whole story: requirements, what each step does, the manual steps if the installer cannot do one (Next Generation Content must be off in the game's Setup dialog), tuning keys, logs and uninstall.

## What differs from upstream

- **Linux build.** The 32-bit `d3d9.dll` proxy, the DXVK fork and the launcher are cross-built with mingw in a container (`toolchain/`), instead of MSVC on Windows.
- **Frame installer.** `frame/install.sh` and `frame/uninstall.sh` install into the Steam copy of the game on SteamOS, with backups, a manifest and a queued Steam shortcut.
- **Frame controller binding.** The Frame's controllers are reached through the Oculus Touch binding, with the left menu button handled for pause and the in-game menu.
- **Frame tuning defaults.** FSAA off, MSAA 2, no comfort vignette, a 110 degree culling cone, no shadows, reflections or water effects, and 72 Hz at resolution 1440 in SteamVR.
- **Stereo rewrite (planned).** One draw call for both eyes through Vulkan multiview in the DXVK fork, with the current two-pass path kept as a fallback.

Everything else is upstream's.

## Building

The mod is cross-built for 32-bit Windows (the game is 32-bit) with mingw in a podman container. The recipe is in `toolchain/`; it produces `d3d9.dll`, `d3d9_dxvk.dll` and `trlvr_launcher.exe`, which together with `openvr_api.dll`, `input/actions.json` (as `trlvr_actions.json`) and the `input/bindings_*.json` files make the payload `frame/install.sh` expects. Upstream's own Windows build (`build.bat`, MSVC) is still in the tree.

## Controls

The layout is upstream's: see [VR CONTROLLER BINDINGS.txt](release/VR%20CONTROLLER%20BINDINGS.txt) (Oculus Touch is the layout that applies on the Frame). One thing is different on the Frame: the **left menu button** is the Frame controller's own menu button, and LegendFrame binds it to pause and the in-game menu. The left system button always opens the SteamVR dashboard. If a button does nothing, reset the binding in SteamVR > Settings > Controllers > Manage Controller Bindings.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- **RototRobot**, author of [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod this is a fork of, including the stereo renderer, tracking, controls and settings panel. Upstream's thanks apply here too: DXVK (Philip Rebohle and contributors), chreden's TRAWindowed, Valve's OpenVR, Khronos OpenXR, Dear ImGui, and Crystal Dynamics for the game.
- Upstream's mod was made with heavy AI (Claude) assistance, as its README says. LegendFrame's Frame work is too.

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists the third-party parts and their licences.

Tomb Raider: Legend is the property of its owners. This is an unofficial fan project, not affiliated with or endorsed by Crystal Dynamics, Eidos, Valve or the upstream author.

## License

Apache License 2.0, as upstream: see [LICENSE](LICENSE) and [NOTICE](NOTICE). Third-party code keeps its own licences (DXVK is zlib/libpng and the shipped `d3d9_dxvk.dll` is a modified build).

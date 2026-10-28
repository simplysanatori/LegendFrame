# LegendFrame

Tomb Raider: Legend in VR on the Valve Steam Frame.

LegendFrame is a Steam Frame fork of **[Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port)** by **RototRobot**: a virtual reality mod for Tomb Raider: Legend with stereoscopic rendering, 6DoF head tracking, motion controllers, a VR third-person camera and an optional first-person mode. All of the VR work is theirs. LegendFrame tracks their current release (TRLVR 1.1.0) and adapts it to run on the Frame, which is an Arm64 Linux headset, through Proton 11 ARM64 and SteamVR, with a single-pass stereo renderer to make it cheaper to emulate. The original README is kept in [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Apache License 2.0, as upstream.

You need your own copy of Tomb Raider: Legend from Steam. No game data is in this repository.

## Status

**0.1.3, in testing on the Frame.** No GitHub release yet. Version numbers: LegendFrame 0.1.x is upstream mod 1.1.0 plus the Frame work in this repository.

What is measured, on one Frame (72 Hz, 1440 px per eye, Proton 11 ARM64, OpenVR):

- Upstream's two-pass stereo: about 40 fps median in gameplay.
- LegendFrame 0.1.1 single-pass: about 51 fps median in gameplay (worst 27 to 39, best 72).
- Menu A/B with the CPU wait for the GPU before `Submit` removed (`submit_wait=0`): 43 to 45 fps became 67 to 70 fps.

What is not confirmed yet: the gameplay effect of `submit_wait=0`, whether it shows tearing or partial frames in the headset, and whether the `god_carry_fix` option cures the god-mode carry smearing. Those are being tested by the owner on 0.1.3 and are not claimed here. The game is a 32-bit x86 program, so the whole thing is emulated; the cost that limits it is the per-draw submission, not the GPU. Details and the numbers are in [docs/FRAME-INSTALL.md](docs/FRAME-INSTALL.md#performance-what-is-measured).

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

- **Single-pass stereo.** One D3D9 draw for both eyes instead of two, done in the DXVK fork by the viewport-index technique (not Vulkan multiview): the draw is issued once with `instanceCount` doubled, a vertex-shader epilogue sets `ViewportIndex = InstanceIndex & 1`, and the per-eye `c0..c3` matrices come from a left/right bank. The images and overlays stay the existing double-wide target. The two-pass path is kept as a fallback: `[vr] stereo_mode=twopass|singlepass`, and a live **Shift+F6** or settings-panel toggle. Design in [docs/DESIGN.md](docs/DESIGN.md), plan in [docs/PLAN.md](docs/PLAN.md).
- **Cheaper per-eye maths.** The proxy computes both eyes' matrices in one call (`stereo_shared_math`, bit-exact against the two-call version) and is built with SSE2 maths.
- **No GPU wait before Submit (option).** `[vr] submit_wait=0` drops the CPU wait for the GPU before the OpenVR submit. Under trial.
- **God-mode carry fix (option).** `[vr] god_carry_fix=1` addresses an upstream carry bug where parts of Lara's mesh stay at the pick-up spot; see [docs/INVESTIGATION-godmode-stretch.md](docs/INVESTIGATION-godmode-stretch.md). Under trial.
- **Linux build.** The 32-bit `d3d9.dll` proxy, the DXVK fork and the launcher are cross-built with mingw in a container (`toolchain/`), on the Frame itself, instead of MSVC on Windows. The toolchain includes an SEH shim (mingw has no 32-bit SEH) and SSE2 flags.
- **Frame installer.** `frame/install.sh` and `frame/uninstall.sh` install into the Steam copy of the game on SteamOS, with backups, a manifest and a queued Steam shortcut.
- **Frame controller binding.** A native Frame controller binding: the left menu button taps for the gear menu and pause, and holds for the settings panel.
- **HUD nudge.** `hud_offset_x` and `hud_offset_y` move the gameplay HUD (also sliders in the settings panel).
- **Frame tuning defaults.** FSAA off, MSAA 2, no comfort vignette, a 110 degree culling cone, no shadows, reflections or water effects, 72 Hz at resolution 1440 in SteamVR, and the OpenVR runtime (OpenXR is not available under Proton 11 ARM64; see the guide).

Everything else is upstream's.

## Building

The mod is cross-built for 32-bit Windows (the game is 32-bit) with mingw in a podman container. The recipe is in `toolchain/`; it produces `d3d9.dll`, `d3d9_dxvk.dll` and `trlvr_launcher.exe`, which together with `openvr_api.dll`, `input/actions.json` (as `trlvr_actions.json`) and the `input/bindings_*.json` files and the `openxr_loader.dll` the mingw `d3d9.dll` links against (install with `--with-openxr`), make the payload `frame/install.sh` expects. Upstream's own Windows build (`build.bat`, MSVC) is still in the tree.

## Controls

The layout is upstream's: see [VR CONTROLLER BINDINGS.txt](release/VR%20CONTROLLER%20BINDINGS.txt) (Oculus Touch is the layout that applies on the Frame). One thing is different on the Frame: the **left menu button** is the Frame controller's own menu button, and LegendFrame binds it directly: a tap opens the gear menu and pauses, and holding it for about half a second opens the settings panel. The left system button always opens the SteamVR dashboard. If a button does nothing, reset the binding in SteamVR > Settings > Controllers > Manage Controller Bindings.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- **RototRobot**, author of [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod this is a fork of, including the stereo renderer, tracking, controls, god mode, physics and settings panel. Without their work there would be nothing to port; the original README is in [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md). Upstream's thanks apply here too: DXVK (Philip Rebohle and contributors), chreden's TRAWindowed, Valve's OpenVR, Khronos OpenXR, Dear ImGui, and Crystal Dynamics for the game.

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists the third-party parts and their licences.

Tomb Raider: Legend is the property of its owners. This is an unofficial fan project, not affiliated with or endorsed by Crystal Dynamics, Eidos, Valve or the upstream author.

## License

Apache License 2.0, as upstream: see [LICENSE](LICENSE) and [NOTICE](NOTICE). Third-party code keeps its own licences (DXVK is zlib/libpng and the shipped `d3d9_dxvk.dll` is a modified build).

# LegendFrame

Tomb Raider: Legend in VR, running on the Valve Steam Frame.

A fork of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) (TRLVR 1.1.0); all the VR work is theirs.

## Features

- 72 Hz most of the time in gameplay
- God mode carries fixed
- Frame controller layout and button prompts
- Movable health bar, ammo readout and item wheel
- Bigger, sharper settings panel
- Install and uninstall scripts

## How it works

- The game runs on the Frame itself. No PC needed.
- Your Steam copy of the Windows game runs through Proton 11 ARM64.
- The mod adds files next to the game and shows it in the headset through SteamVR.

## Requirements

- A Steam Frame with SteamOS and SteamVR
- Your own **Tomb Raider: Legend** from Steam, started once from its store entry
- **Proton 11 (ARM64)** installed in Steam
- A terminal: SSH, or Konsole in desktop mode

## Install

1. Download `legendframe-1.0.1-steam-frame-linux-arm64.zip` and `SHA256SUMS` from [Releases](https://github.com/simplysanatori/LegendFrame/releases), then check the zip:

   ```sh
   cd ~/Downloads
   sha256sum -c --ignore-missing SHA256SUMS
   ```

2. Get the installer:

   ```sh
   git clone -b steam-frame https://github.com/simplysanatori/LegendFrame.git
   cd LegendFrame
   ```

3. With the game closed, install:

   ```sh
   frame/install.sh --payload ~/Downloads/legendframe-1.0.1-steam-frame-linux-arm64.zip
   ```

4. Restart Steam (Power > Exit Steam, then start Steam again).
5. Launch **Tomb Raider: Legend VR**, not the store entry.

Using [Frametanium](https://github.com/Cogential/frametanium)? Add `simplysanatori/LegendFrame` there.

Using [FrameDrop](https://framedropvr.com)? [Install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FLegendFrame%2Freleases%2Flatest%2Fdownload%2Flegendframe.framedrop.json).

## First run

1. The game shows its **Setup** dialog.
2. Turn **Next Generation Content** off, or the VR view is black.
3. Leave the other options as they are.

## Controls

| Control | In play |
|---|---|
| Left stick | Move; double click recentres |
| Right stick | Turn |
| A / B | Jump / crouch |
| X / Y | Precision aim / grenade |
| D-pad | Up medipack, right binoculars, down weapons, left light |
| Menu (right) | Pause; hold for settings |
| View (left) | PDA |
| Left / right trigger | Action / fire |
| Left / right grip | Grapple / lock on |

[All controls](docs/USER_GUIDE.md#controls) and [settings](docs/USER_GUIDE.md#settings) are in the user guide.

## Update

Download the new zip, then in `LegendFrame`:

```sh
frame/install.sh --payload ~/Downloads/legendframe-<version>-steam-frame-linux-arm64.zip
```

## Uninstall

In `LegendFrame`:

```sh
frame/uninstall.sh
```

## Troubleshooting

- **Game starts flat.** Launch **Tomb Raider: Legend VR** instead.
- **A floating window covers the view.** Press "Connect to dock" on it once.
- **A button does nothing.** Reselect the LegendFrame binding in SteamVR > Settings > Controllers > Manage Controller Bindings.
- **Installer says to run the game once.** Run it from the store entry, quit, then install again.

More fixes: [user guide](docs/USER_GUIDE.md#troubleshooting).

## Reporting a problem

Open an [issue](https://github.com/simplysanatori/LegendFrame/issues): what you did, expected and saw.

Attach `trlvr.log` from the game folder ([other logs](docs/USER_GUIDE.md#logs)). Check it for personal details first.

## More

- [User guide](docs/USER_GUIDE.md)
- [AGENTS.md](AGENTS.md): for developers and AI agents
- [Building from source](AGENTS.md#build-from-source)
- [Changelog](docs/CHANGELOG.md)

## Support

Support RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) first: star it, report bugs there, send improvements upstream.

For more Steam Frame ports, [buy me a coffee](https://ko-fi.com/sana748653). It pays for Claude API credits.

## Credits

- [simplysanatori](https://github.com/simplysanatori): built LegendFrame
- **RototRobot**, [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod
- DXVK, by Philip Rebohle and contributors
- Valve's OpenVR, Khronos OpenXR, Dear ImGui, chreden's TRAWindowed
- Crystal Dynamics: the game

Full list and licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Tomb Raider: Legend is the property of its owners. This is an unofficial fan project, not affiliated with or endorsed by Crystal Dynamics, Eidos, Valve or the upstream author.

## Licence

Apache License 2.0, as upstream: see [LICENSE](LICENSE) and [NOTICE](NOTICE).

## How this was made

I chose to make RototRobot's mod run well on my Frame, with partial assistance from Claude. I tested and tuned it in the headset. Upstream's README says it was made with heavy AI assistance, so the code underneath is AI assisted too.

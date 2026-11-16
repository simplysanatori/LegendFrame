# LegendFrame

Tomb Raider: Legend in VR on the Valve Steam Frame.

A fork of RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) (TRLVR 1.1.0); all the VR work is theirs.

## Features

- 72 Hz most of the time in gameplay
- God mode carries fixed
- Frame controller layout and button prompts
- Movable health bar, ammo readout and item wheel
- Bigger, sharper settings panel

## How it works

The game runs on the Frame itself. The mod adds files to your Steam copy and shows the game in the headset through SteamVR.

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
5. Launch **Tomb Raider: Legend VR**. The store entry starts the game flat.

Other installers:

- [Frametanium](https://github.com/Cogential/frametanium): add `simplysanatori/LegendFrame`.
- [FrameDrop](https://framedropvr.com): [Install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FLegendFrame%2Freleases%2Flatest%2Fdownload%2Flegendframe.framedrop.json).

## First run

In the game's **Setup** dialog, turn **Next Generation Content** off, or the VR view is black.

## Controls

| Control | In play |
|---|---|
| Left stick | Move; double click recentres |
| A / B | Jump / crouch |
| X / Y | Precision aim / grenade |
| D-pad | Up medipack, right binoculars, down weapons, left light |
| Menu (right) | Pause; hold for settings |
| View (left) | PDA |
| Left / right trigger | Action / fire |
| Left / right grip | Grapple / lock on |

Every [control](docs/USER_GUIDE.md#controls) and [setting](docs/USER_GUIDE.md#settings) is in the user guide.

## Update

Download the new zip, then in `LegendFrame`:

```sh
git pull
frame/install.sh --payload ~/Downloads/legendframe-<version>-steam-frame-linux-arm64.zip
```

## Uninstall

In `LegendFrame`:

```sh
frame/uninstall.sh
```

## Troubleshooting

- **A floating window covers the view.** Press "Connect to dock" on it once.
- **A button does nothing.** Reselect the LegendFrame binding in SteamVR > Settings > Controllers > Manage Controller Bindings.

[More fixes](docs/USER_GUIDE.md#troubleshooting)

## Reporting a problem

Open an [issue](https://github.com/simplysanatori/LegendFrame/issues): what you did, expected and saw. Attach `trlvr.log` from the game folder ([other logs](docs/USER_GUIDE.md#logs)). Check it for personal details first.

## More

- [User guide](docs/USER_GUIDE.md)
- [AGENTS.md](AGENTS.md): for developers and AI agents
- [Building from source](AGENTS.md#build-from-source)
- [Changelog](docs/CHANGELOG.md)

## Support

Star RototRobot's [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port) and send improvements upstream.

For more Steam Frame ports, [buy me a coffee](https://ko-fi.com/sana748653).

## Credits

- [simplysanatori](https://github.com/simplysanatori): built LegendFrame
- **RototRobot**, [Tomb-Raider-Legend-VR-Port](https://github.com/RototRobot/Tomb-Raider-Legend-VR-Port): the VR mod
- DXVK, by Philip Rebohle and contributors
- Valve's OpenVR, Khronos OpenXR, Dear ImGui, chreden's TRAWindowed
- Crystal Dynamics: the game

Full list and licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Tomb Raider: Legend is the property of its owners. This is an unofficial fan project, not affiliated with or endorsed by Crystal Dynamics, Eidos, Valve or the upstream author.

## Licence

Apache License 2.0, as upstream ([LICENSE](LICENSE), [NOTICE](NOTICE)).

## How this was made

I started this project and used AI (Claude) to speed up development. I write, review and fix code by hand all the way through, keep the AI's work checked and reined in, and aim to keep the port as lightweight as possible. I test it on my own Steam Frame. Upstream's mod was also made with AI assistance, as its README says.

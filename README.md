# HoverNet

HoverNet is a fast-paced hovercraft racing game: pick a track, race other hovercraft around tight, ramp-filled circuits, and use missiles to take out the competition. It's a modernization of the original HoverRace, created by GrokkSoft in 1996, with a native Windows client, a Linux client, and an online multiplayer lobby so you can race against other players over the internet.

## Playing online

Launch the game and open the multiplayer lobby to see races other players have open, or host your own. The first time you connect you'll be asked to pick a username, so other players can see who's racing. From the lobby you can:

- See a live list of open races and who else is currently in the lobby.
- Host a race on any track, choosing lap count and whether weapons are allowed.
- Join a race in progress or waiting to start.
- Chat with other players while you wait.

## Getting the game

Downloads for both Windows and Linux are published on the [Releases page](../../releases).

- **Windows 32 bit**: Classic MFC forums layout. Download and run the installer (`HoverNet-setup-32.exe`).
- **Windows 64 bit**: Experimental cross-platform SDL2/IMGUI layout. Download and run the installer (`HoverNet-setup-64.exe`).
- **Linux**: download the `.deb` package and install it (`sudo apt install ./hovernet-game_*.deb`), then launch it from your applications menu or run `hovernet` from a terminal.

### Community tracks

HoverNet has seven official tracks and about a thousand community-made ones from
the classic HoverRace library. You don't need to install anything: open the
track dropdown (Local Play, or Host race online) and the community tracks are
listed below the official ones, with a search box. Pick one and press
**Download & Start Race**; HoverNet downloads that single track (usually under
a megabyte), checks it, and starts the race. Joining someone else's race on a
community track you don't have works the same way: **Download & Join**.

- **Download everything:** Local Play has a **Download all community tracks**
  button (about 150 MB), or from a terminal:
  `hovernet --download-community-tracks --all` (or give track names instead of `--all`).
- **Needs:** an internet connection and `curl` (or `wget`) on Linux. Windows 10
  and later already include `curl.exe`. If it's missing the game tells you what to
  install: `sudo apt install curl`.
- **Where they go:** `~/.config/hovernet/CommunityTracks` on Linux
  (`%APPDATA%\HoverNet\CommunityTracks` on Windows). Delete files there to free space.
- **Offline:** download the full community pack from the release page and install it
  with `scripts/install-community-tracks.sh` (see
  [Community tracks](docs/community-tracks.md)), or install the
  `hovernet-community-tracks` `.deb`.

## Controls

| Input | Action |
| --- | --- |
| Left Shift / Right Shift | Accelerate |
| Left / Right | Steer; select hovercraft during the countdown |
| Up | Jump |
| Down | Brake / reverse |
| Left Ctrl or Right Ctrl | Fire missile |
| Tab | Select weapon |
| F3 | Following camera |
| F4 | Cockpit camera |
| Escape | Open the in-race menu; leave an online race or quit |

## Building from source and technical documentation

If you want to build HoverNet yourself, or you're interested in how the client, race server, and CI pipelines work, see the [technical overview](docs/technical.md).

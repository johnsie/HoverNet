# HoverNet

HoverNet is a fast-paced hovercraft racing game: pick a track, race other hovercraft around tight, ramp-filled circuits, and use missiles to take out the competition. It's a modernization of the original HoverRace, with a native Windows client, a Linux client, and an online multiplayer lobby so you can race against other players over the internet.

## Playing online

Launch the game and open the multiplayer lobby to see races other players have open, or host your own. The first time you connect you'll be asked to pick a username, so other players can see who's racing. From the lobby you can:

- See a live list of open races and who else is currently in the lobby.
- Host a race on any track, choosing lap count and whether weapons are allowed.
- Join a race in progress or waiting to start.
- Chat with other players while you wait.

## Getting the game

Downloads for both Windows and Linux are published on the [Releases page](../../releases).

- **Windows**: download and run the installer (`HoverNet-setup.exe`).
- **Linux**: download the `.deb` package and install it (`sudo apt install ./hovernet-game_*.deb`), then launch it from your applications menu or run `hovernet` from a terminal.

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

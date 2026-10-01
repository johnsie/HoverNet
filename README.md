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

HoverNet lists seven official tracks and can also play about a thousand
community-made ones from the classic HoverRace library. They are a separate
download (about 163 MB), so the game only shows the official tracks until you
install them. The community tracks then appear under a "Community tracks"
heading below the official ones, with a search box, in both the Local Play setup
and the online Host race dialog.

**Getting them working on Linux**

1. Install the game as above (the `hovernet-game` `.deb`).
2. Download the community track pack from the release page. It comes as either
   `hovernet-community-tracks_*.zip` or `hovernet-community-tracks_*_all.deb`.
3. Install it with whichever you downloaded:
   - **`.deb`** (system-wide, needs `sudo`):
     `sudo apt install ./hovernet-community-tracks_*_all.deb`
   - **`.zip`** (just for you, no `sudo`): unzip it, then from a copy of this
     repository run `scripts/install-community-tracks.sh hovernet-community-tracks_*.zip`.
     This copies the tracks to `~/.config/hovernet/CommunityTracks`. You can also
     copy the `.trk` files there yourself.
4. Start HoverNet and open Local Play. The track dropdown now lists the community
   tracks after the official ones. If you only see the seven official tracks, the
   files are not in one of the folders above.

To play a community track online, everyone in the race needs the pack installed,
and the race server must allow the track (servers running this version of
HoverNet do). If you are missing a track someone else hosted, the lobby disables
Join and tells you why.

See [Community tracks](docs/community-tracks.md) for Windows, running a server,
and how the tracks were made to work.

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

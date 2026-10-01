# Community tracks

HoverNet ships with seven **official** tracks. The classic HoverRace community
made about a thousand more, and the game can list and play them too. They are
used exactly as published: no track file is modified, the engine was made to
cope with them instead.

## What players see

Every track selector (Local Play setup and the online *Host race* dialog) shows
the official tracks first, under an **Official tracks** heading, always in the
same order. Below them is a **Community tracks** heading with the library sorted
by name and a search box at the top of the list. Tracks marked *free play* have
no finish line, so no laps can be completed; the HUD shows the elapsed time
instead of a lap counter and the laps setting is greyed out.

Community tracks are a separate download (about 163 MB zipped) and are only
listed when installed. A player without the pack just sees the official tracks.

## Installing the pack

| Platform | How |
| --- | --- |
| Linux (Debian/Ubuntu) | `sudo apt install ./hovernet-community-tracks_*_all.deb` |
| Linux (any) | `scripts/install-community-tracks.sh hovernet-community-tracks_*.zip` |
| Windows | `powershell -File scripts\install-community-tracks.ps1 -Source hovernet-community-tracks_*.zip` |
| Manual | unzip the `CommunityTracks` folder into `NetTarget` beside the game |

The game searches, in order: `HOVERNET_COMMUNITY_TRACKS_DIR` (if set),
`NetTarget/CommunityTracks` beside the game, and the per-user folder
(`~/.config/hovernet/CommunityTracks` on Linux, `%APPDATA%\HoverNet\CommunityTracks`
on Windows).

## Online races

Everyone in a race needs the same track, so a player cannot join a race on a
community track they have not installed: *Join* is disabled and the lobby says
why. The race server must also allow the track. It reads the community
manifest (`CommunityTracks.tsv`) and relays races only for names it lists, plus
the official seven. It searches `$HOVERNET_TRACK_MANIFEST`,
`/etc/hovernet/CommunityTracks.tsv` (installed by the server package, and
editable by the operator), `/usr/share/games/hovernet/NetTarget/CommunityTracks.tsv`,
then `NetTarget/CommunityTracks.tsv` and `CommunityTracks.tsv` in the working
directory. Without one, only the official tracks are accepted. The server never
needs the `.trk` files.

## The manifest

`NetTarget/CommunityTracks.tsv` is committed to the repository (the tracks are
not). Each line is `name<TAB>race|freeplay<TAB>starts<TAB>rooms<TAB>warnings`.
A track is listed only if it is in the manifest **and** its file is present, and
names only ever reach the filesystem through this catalog, so a hostile track
name sent by a remote host cannot escape the track folders.

To add or refresh tracks, put the `.trk` files in a folder and run:

```sh
scripts/generate-community-manifest.sh build/linux/HoverNetTrackValidator PATH/TO/TRACKS
```

Each track is run through `HoverNetTrackValidator --community`. Tracks that do
not load or have no usable start are reported and left out; tracks with the same
name as an official track are skipped (the official one always wins).

Build the distributable pack with `scripts/package-community-tracks.sh VERSION PATH/TO/TRACKS`
(a zip and a `.deb`; it refuses to build if any listed track is missing).

## The community compatibility tier

`HoverNetTrackValidator` has two levels. The default is the strict 2.0 contract
for bundled tracks. `--community` only rejects what the game genuinely cannot
play and reports everything else as a warning:

| Rejected (error) | Allowed (warning) |
| --- | --- |
| cannot load, or data damaged / unsupported objects | no finish line or checkpoints (becomes *free play*) |
| no rooms, or no start | disconnected rooms, one-way links |
| a start outside its room or with an invalid room | rooms with fewer than three vertices, wrong winding, no clearance |
| a room link outside the room table | overlapping starts, a single start (players share it) |

Of the 1,012 tracks in the library, all pass: 805 are races and 207 are free
play. (Three share a name with, and are identical to, official tracks, leaving
1,009 listed.)

## What had to change in the engine

The classic tracks use features and malformed data the Linux engine did not yet
handle. Without touching any track, the loader and renderer now:

- build the classic default, wood, fire and brick surfaces (object classes 1-5)
  and the animated, pushable "electro car" / "demo fighter" elements (10, 13),
  which 23 tracks reference;
- read sections with fewer than three vertices at their true size (a safety
  clamp misaligned the rest of the file for several tracks);
- reject implausible audible-room counts instead of allocating gigabytes and
  looping for minutes, and bounds-check room links, texture tables and trig
  table indexes (negative angles);
- skip zero-height walls instead of dividing by zero in the renderer;
- start a race with fewer start positions than players by sharing them.

## Licence

The tracks were authored by many people and mirrored from the old OpenHover
site. No licence or authorship was recorded with them, so redistribution rights
are unknown. They are provided as-is, as a separate pack, and are not part of
the game's own licence.

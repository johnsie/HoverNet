# Replays: recording, verifying and watching

A replay is a small file (about 4 bytes per tick per player) holding a race's track identity, its settings and every player's
input on every tick. Because the simulation is deterministic, any HoverNet client on any platform plays it back exactly as the
race happened; a state hash every 250 ticks lets anything detect a replay that no longer reproduces. The format is in
[specs/replay-format.md](specs/replay-format.md).

## Watching one in the game

    HoverNet --replay race.hnr [--replay-player N]

(`HoverNetGame2Player` on Linux.) The game plays the race back with its normal renderer, from the point of view of player `N`
(default 0, the first on the grid). **P** pauses, **1** to **5** set the speed (a quarter, a half, normal, double, four times), **Left** and **Right** jump five seconds back and forward and **Home** restarts, **,** and **.** jump to two seconds before the previous or next lap crossing, clicking the timeline bar along the bottom seeks to that point (the bar shows the time and speed and a mark at every lap crossing and finish, noted as the replay plays through it; `scripts/test-replay-viewer.sh` checks the marks equal `HoverNetReplay events`), **C** steps the chase camera through 50% to 300% of the classic distance ( also for a spectator; only the picture changes), **[** and **]** switch the camera and HUD to the previous or next craft (the game keeps a snapshot of the race every 250 ticks as it plays and seeks from the nearest one),
**Esc** opens the pause menu. The game refuses a replay recorded under different game rules, or on a different version of the
track, and says so; it never plays something that would not reproduce.

## Where replays come from

- **The dedicated server** writes one for every race when started with `--replay-dir DIR`: the official record of the race,
  made from the inputs the server actually simulated (`HoverNetAuthServer`, [authoritative-race.md](authoritative-race.md)).
- **The replay tool** records scripted races for testing: `HoverNetReplay record <track> <ticks> <out.hnr> [--players=N]`.

## Checking and debugging

- `HoverNetReplay results race.hnr` replays the inputs through the authoritative race and prints one row per player (rank, laps, finish
  time, best lap), the same on every platform: the input a leaderboard or a time trial needs. The server writes the same table beside each official replay as `<name>.results.tsv`
  (tab-separated: rank, player, laps, finished, finish_ms, best_lap_ms), and its test checks the two agree.
- `HoverNetReplay events race.hnr` prints the timeline: every lap crossing and finish with its tick, lap time and race time. The server records the same events as the race runs (`AuthoritativeRace::Events`) and writes them in its event log; they are derived, not stored in the file, so the replay format did not change.
- `HoverNetReplay leaderboard a.hnr b.hnr ... [--track=NAME] [--top=N]` re-simulates every replay and ranks the finishers (per track and lap count)
  and the best laps (per track). A replay that is damaged, recorded under other rules or that does not reproduce is reported as skipped
  and never ranked (exit status 1). Entries are named `id#slot`, where the id is what `HoverNetReplay id race.hnr` prints (16 hex digits of the file's SHA-256, the same whatever the file is called); replays carry player slots, not accounts. The scripted bots in the
  test suite do not finish a race, so `HoverNetDriver` (`NetTarget/LinuxClient/DriverTool.cpp`) finds controls that do: a beam search over
  state snapshots of the real simulation, ranked by laps and checkpoints, keeping only craft in track cells not seen yet. It writes the
  replay the server would record. The test `HoverNetLeaderboard` (`scripts/test-leaderboard.sh`, Linux) makes two different finishing
  drives of ClassicH (one lap, 99.5 s and 260.9 s), checks both verify, that `results` and `events` report the finish, and that the
  leaderboard ranks them in order and honours `--top`. Checked on Linux only; the search is deterministic for given options, but it is a
  heuristic and finds no finish on every track or seed (it found none on The River with the default options), so the test names ClassicH and fixed seeds.
- `HoverNetReplay verify race.hnr` re-simulates it and compares every checkpoint; `info` prints the header.
- `HoverNetReplay trace race.hnr` prints every tick's state hash and `dump race.hnr <tick>` the whole state at a tick; diffing the
  output from two platforms finds the first tick and the field that differ ([determinism.md](determinism.md)).

## How it is tested

`scripts/test-replay-viewer.sh` (ctest `HoverNetReplayViewer`) records replays on several tracks with 2, 4 and 8 players, plays
each through the real game client in a headless window, and requires the client's final state hash to equal the replay tool's, for
different watched players. It passes on Linux (also under ASan/UBSan) and on Windows x64, and the hashes are identical on both,
so a replay made on one platform is the same race on the other.

## Not yet

A replay browser in the menus, saving a replay of a race you played yourself (the local game is not run on the fixed 16 ms tick
yet, see [ADR 0001](adr/0001-simulation-core.md) step 6), switching the watched player during playback, and a free camera.

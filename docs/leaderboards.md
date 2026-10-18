# Leaderboards: the community's best times, per craft

**Leaderboards** (main menu) shows the best times of everyone who races on the HoverNet server you play on, not just the ones on your own device.

- **Per track and per craft.** Pick a track; each craft has its own board of the **best five** (Basic, Hi-Tech, Bi-Turbo, Eon, Manta), because the craft
  change how fast a hovercraft is. A record in one craft never replaces a record in another: breaking the Bi-Turbo record leaves the Basic one where it was.
  A craft with no times yet says so.
- **Fastest laps or fastest races.** *Fastest laps* are single laps, whatever the length of the race they were driven in. *Fastest races: N laps* are the
  finishing times of races of N laps (the lap counts that have records are offered).
- **Names.** Each time shows the display name the player had in that race, and the date. Names are what players typed: nothing proves a name belongs to the
  person using it until accounts exist ([online-services.md](online-services.md)).
- **WATCH** downloads the replay the time came from and plays it, following the craft that set it. Every time comes from a replay the server simulated, so it
  can be checked by anyone with `HoverNetReplay verify`.
- **Your own best** on this device is shown above the lists for comparison (it does not say which craft).

How it is built: when a race ends, the server writes a results table beside the replay (rank, slot, laps, times, **the display name** and **the craft** each
player drove: picked during the countdown, it cannot change once the race runs). The leaderboards are computed from the newest `--replay-keep` replays and
their tables, so a record is kept for as long as the replay it came from is, not forever (the oldest replays are removed as new ones arrive). Races that
were saved before the craft was recorded have no craft and are not on any board. A server must be started with `--replay-dir` to have any.

Craft names are those of `Authority/CraftNames.h`, in the order the pick-your-craft screen cycles through them.

Tests: `HoverNetReplayShare` (boards per craft, the best five, lap counts, names, fitting one network frame), `HoverNetOnlineLobby` (the client against a real
server) and `HoverNetLeaderboards` (the screen against a real server: a board per craft with names, and WATCH plays the record's run to the verified hash).

Not yet: all-time records that outlive the replays, named boards tied to accounts, records for community tracks, filters by date.

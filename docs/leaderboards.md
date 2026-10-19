# Leaderboards: the community's best times, per craft, recent winners and the monthly league

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

## Records broken

The first tab, **Records broken** (the screen opens on it), lists the newest results that beat the best time on their board (or are the first on it), newest first: the track, the date, whether it was the fastest lap
or the fastest race of that many laps, the craft, who set it, the time and the holder and time it beat, with a **WATCH** button for the replay. A board is a
track, a craft and a lap or race length, as on the All records tab. The first result on a board *sets* a record and is listed too ("set the first record": most boards of the craft other than the Basic were empty when the craft pick began to work online, and a first time is a record); a tie does not break one (the older run keeps
it); one race can break both the lap and the race record, but in a race of one lap the finish is the lap, so that result is listed once (as the lap). It is replayed from the results tables of the replays the server keeps, oldest first, so it only knows
the history that is still kept: after old replays are removed, the oldest kept result on a board counts as the first. Community tracks are included: All records picks any track of the library (with a search box), and WATCH on a time on a community track downloads the track first if this computer does not have it. Like the All records tab it includes every race,
not only those that count for the league. Test: `HoverNetReplayShare` (the rules and the lobby message) and `HoverNetLeagueTabs` (the tab against a real server).

## Recent winners and the monthly league (2.3.18)

The Leaderboards screen has four tabs: **Records broken**, **All records** (the per-craft boards above), **Recent winners** and **Monthly league**.

- **Recent winners** lists the newest races won, with the track, date, laps, number of players, the winner's name and craft and a *Watch the race* button
  that plays it from the winner's seat. **A race is listed only if two or more players were in it and it passed the league's checks below.** Races with a
  single player, and races one person made against themselves, never appear.
- **Monthly league.** In a race that counts, the winner scores **3 points**; second and third score nothing (otherwise the easiest way to collect points would be to start many two-player races). The standings are per UTC calendar month and **start again
  on the 1st**; the screen also shows the winner of every finished month, this year's standings so far, and the **winner of each finished year**
  (announced when the year ends). The month selector shows the standings of an earlier month.

**What counts (the cheat checks, `Authority/League.h`):**

1. At least two players **finished** (someone who stays on the grid or leaves is not a competitor); a race of one scores nothing.
2. Every player sent a **device id**: a random number each installation makes once and keeps (the browser build keeps it with its other settings). No two
   players may share a device id, and no two may use the same display name. One person racing themselves from two browser tabs, two windows or two copies
   on the same computer shares a device id, so that race is not valid and scores for nobody. **IP addresses are never used**, because players share them
   (families, schools, mobile networks).
3. A duel (exactly two devices) between the same two devices scores at most **3 times per UTC day**, so two accounts of one person cannot farm points from
   each other.
4. A game too old to send a device id (before 2.3.18) cannot score, and a race it is in is not valid. Game and server should be updated together.

**What this cannot do:** the device id is chosen by the player's own game, so someone who edits it by hand can pretend to be another device, and a person
with two different computers is two devices. Only accounts ([online-services.md](online-services.md), Stage 6a) can prove one person is one player. The
rules make cheating take effort and cap what it gains per day; they do not make it impossible. The points belong to the device; the name shown is the one it
used last.

How it is built: the server appends every race that counts, and each score, to `league.tsv` (in the replay folder, or `--league-file`), which survives
restarts; the standings are computed from it. The results table beside each replay has a `valid` column (1 when the race passed checks 1-2) that
the recent-winners list reads. A server without `--replay-dir` keeps the league in memory only.

Tests: `HoverNetLeague` (points, months, years, every cheat rule), `HoverNetLeagueChecks` (real races over TCP with scripted players on the same and on
different devices), `HoverNetReplayShare` (the winners list and the lobby messages) and `HoverNetLeagueTabs` (the tabs against a real server with a seeded
league).

Tests: `HoverNetReplayShare` (boards per craft, the best five, lap counts, names, fitting one network frame), `HoverNetOnlineLobby` (the client against a real
server) and `HoverNetLeaderboards` (the screen against a real server: a board per craft with names, and WATCH plays the record's run to the verified hash).

Not yet: all-time records that outlive the replays, named boards tied to accounts, records for community tracks, filters by date.

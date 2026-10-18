# Ghosts: racing your own best run

When you set a new personal best finish on a track (and lap count), the game also keeps **the path you drove** as a ghost. In your next race on
the same track and lap count the ghost is drawn beside you, so you race your own best time. It is on by default; **Settings, "Show my best run as
a ghost"** switches it off (a new best is still saved).

- **Local, desktop and browser alike.** The ghost is a small file (`ghost_<track>_<laps>.bin`, about 20 bytes per 48 ms of racing, 40 KB for a
  two-minute run) in the same per-user settings folder as the personal bests (`~/.config/hovernet`, `%APPDATA%\HoverNet`; the browser keeps it in
  IndexedDB with the other settings). Nothing is sent anywhere. Delete the file to forget the ghost.
- **Only drawn.** The ghost is not a craft in the race: it cannot hit anything or be hit, does not change the state hash, and is not sent to
  a server, so it works the same in a local race and a race on a HoverNet server. It is drawn with the craft of the room it is in, so walls hide
  it like any other craft. It uses the same craft model as yours in another colour, and disappears a second after it finishes.
- **One ghost per track and lap count**, replaced only by a faster finish. A race you quit does not save one.
- **Format** (`GhostRun.h`): `HNGHOST1`, laps, finish time, track name, then samples (time, position, orientation, room) at most every 48 ms;
  the game draws between two samples. A file that is truncated, oversized (limits in the header) or whose times do not increase is ignored.

Tests: `HoverNetGhostRun` (the format, interpolation, damaged files, file names) and `HoverNetGhost` (`scripts/test-ghost.sh`: the real client
saves a ghost on a new best, shows it in the next race, does not replace it with a slower run, hides it when the setting is off).

Not yet: a gap to the ghost on the HUD, a ghost taken from a server replay or a friend's run, several ghosts, and a ghost in the web client's
real-browser test (it builds and shares the code; the headless browser test does not finish a race).

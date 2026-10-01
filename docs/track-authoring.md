# Track authoring

The supported HoverNet 2.0 workflow is a text source file, the command-line
maze compiler, and the command-line validator. HoverCad is explicitly a legacy
tool: it remains in the repository for historical `.TR` work, but it is not a
supported or required part of producing a 2.0 track.

`NetTarget/TrackSources/Tidal Causeway.track.txt` is the maintained example
project. It demonstrates connected rooms, eight starts, water features, jumps,
pickups, collision walls, and the three required race gates.

## Build and validate

From the repository root:

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --target HoverNetMazeCompiler HoverNetTrackValidator
build/linux/HoverNetMazeCompiler \
  "NetTarget/Tracks/My Track.trk" "NetTarget/TrackSources/My Track.track.txt"
build/linux/HoverNetTrackValidator "NetTarget/Tracks/My Track.trk" "My Track"
```

Commit both the editable `.track.txt` source and compiled `.trk`. Register the
display name in the shared client's `kHostableTracks`/`kTrackGuides` tables and
the RaceServer host whitelist. Add it to the validation list in `CMakeLists.txt`.

## Release requirements

- Use clockwise room polygons with at least three distinct vertices and useful
  vertical clearance. Every neighbour link must be reciprocal and all rooms
  must form one connected course.
- Supply 8–10 distinct starting positions. Each must be inside its declared
  room and within that room's vertical bounds.
- Supply at least one finish (`1,202`), checkpoint 1 (`1,203`), and checkpoint 2
  (`1,204`). Wide gates may use more than one element of a gate type.
- Avoid zero-length collision walls, blind traps, long featureless straights,
  and narrow geometry that can pin a hovercraft.
- Put speed pads after technical obstacles or on acceleration lines, never
  immediately before a jump. Mark jumps, pads, pickups, and the start/finish
  line visually.
- Test local and two-client online completion with weapons both enabled and
  disabled. Automated coverage checks loading, joining, synchronized startup,
  and bounded play on every course; human playtesting remains responsible for
  feel and route readability.

## Content balance envelope

The 2.0 release keeps established hovercraft physics and weapon timing to avoid
invalidating existing tracks. New courses should offer alternate racing lines
rather than mandatory pickups. Fuel must be reachable, mines and missile
pickups must not cover the only safe line, and speed pads need braking distance
before walls or jumps. A weapons-disabled race must prevent selecting or firing
missiles, mines, and power-ups; the lobby setting is propagated to all clients.

// Starting points for a NEW track: the editor is for making tracks, not only for opening existing ones (docs/roadmap-track-editor.md, Phase 2,
// "New track"). A template builds a complete, valid TrackModel with the editor core's own types, so the New Track command, the tests and a future
// scripting interface all start from the same code. Every template is checked by scripts/test-new-track.sh: it must compile with the real compiler,
// pass the strict validator and the strict lint, and be finishable by the game's own search driver (HoverNetDriver).
#pragma once

#include "TrackSource.h"

namespace HoverNet
{
struct NewTrackOptions
{
    std::string name = "New Track";
    int rooms = 24;             // how many rooms the loop is cut into (8 to 96)
    double radius = 140.0;      // the middle of the road, in metres from the centre
    double halfWidth = 14.0;    // half the road width
    bool pickups = true;        // a few power-ups, a fuel source and a speed pad
};

// A closed loop of rooms with the eight starts, the finish line and both checkpoints in place, travelling anticlockwise.
// Returns false and sets error when the options cannot make a track (too few rooms, a road wider than the loop, ...).
bool MakeRingTrack(const NewTrackOptions& options, TrackModel& model, std::string& error);
}  // namespace HoverNet

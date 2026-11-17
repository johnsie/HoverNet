// The real track, in 3D, for a menu to show behind its panels: the race view of a craft on the track, drawn once by the game's own renderer (MR_Observer, with the camera exactly as the
// race has it: this file never moves, sets or reads the camera) into a picture. One at the start line and one a few seconds into a scripted drive, so a screen can fade between them.
// Presentation only: it builds its own throwaway session and never touches a running race.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HoverNetBackdrop
{
struct Picture { int width = 0, height = 0; std::vector<std::uint8_t> rgba; };

// Draws `frames` pictures of the track at `path` (a .trk), the first at the start and the others every `ticksBetween` simulation ticks of scripted driving. Returns the number drawn (0 = failed).
int Render(const std::string& path, const std::string& name, int width, int height, int frames, int ticksBetween, std::vector<Picture>& out);
}

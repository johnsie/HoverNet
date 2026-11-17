// A plan view of a track for menus: the floor as a lit shape (higher floors lighter, so bridges and ramps show), a bright edge, and the
// pads on it in colour (fuel red, boost orange, finish line gold, power-ups cyan). Built from the loaded level, so it is the real
// track, not a stored picture. Presentation only: it reads a level and never touches a race.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HoverNetPlan
{
struct Plan
{
    bool ok = false;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;   // width * height * 4, straight alpha
    int rooms = 0, fuelPads = 0, boostPads = 0, powerUps = 0, finishLines = 0;
    // The shapes themselves, for drawing the track tilted and turning: x and y are in units of the track's longer side, centred on 0; h is the floor height 0..1.
    struct Shape { std::vector<float> xy; float h = 0; };
    struct Mark { float x = 0, y = 0, h = 0; int kind = 0; };   // kind: 0 fuel, 1 boost, 2 finish line, 3 power-up
    std::vector<Shape> shapes;
    std::vector<Mark> marks;
    double lengthKm = 0.0;            // the length of the shortest lap-shaped route is not known here: 0 = unknown
};

// Loads `trackPath` (a .trk) as a level and draws it so its longer side is `maxSide` pixels. Returns ok == false when it cannot be loaded.
Plan BuildPlan(const std::string& trackPath, const std::string& trackName, int maxSide);

// The colours the plan uses, so a legend and the in-race map can match.
struct Colours { std::uint8_t fuel[3], boost[3], finish[3], power[3]; };
const Colours& PadColours();
}

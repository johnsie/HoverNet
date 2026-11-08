#include "TrackTemplates.h"

#include <cmath>
#include <cstdio>

namespace HoverNet
{
namespace
{
const double kPi = 3.14159265358979323846;

// The element ids are the game's (TrackElementCatalogue.h).
const int kPowerUp = 152, kFuel = 200, kSpeedPad = 201, kFinish = 202, kCheckpoint1 = 203, kCheckpoint2 = 204;

struct Point { double x, y; };

// Authored coordinates are whole millimetres, as in the bundled tracks. Rounding here (not just when writing) keeps the text clean and makes a template
// produce the same file on every platform: the C libraries' sin and cos differ in the last few bits, which would otherwise show in the 17th digit
// (found by comparing the WebAssembly and native output, 8 November 2026). Negative zero is written as zero.
double Millimetres(double metres)
{
    const double rounded = std::floor(metres * 1000.0 + 0.5) / 1000.0;
    return rounded == 0.0 ? 0.0 : rounded;
}

// A point at `fraction` of the way through room `room` (0 is its start edge, 1 its end edge) and `lateral` metres from the middle of the road
// (positive is the outer side, which is the right-hand side of an anticlockwise lap).
Point OnRoad(const NewTrackOptions& o, int room, double fraction, double lateral)
{
    const double delta = 2.0 * kPi / o.rooms;
    const double theta = delta * (room + fraction);
    const double r = o.radius + lateral;
    return Point{Millimetres(r * std::cos(theta)), Millimetres(r * std::sin(theta))};
}

// The direction of travel there, in degrees (the compiler reads Orientation as degrees, 360 to a turn).
double Heading(const NewTrackOptions& o, int room, double fraction)
{
    const double delta = 2.0 * kPi / o.rooms;
    double degrees = (delta * (room + fraction) + kPi / 2.0) * 180.0 / kPi;
    while (degrees >= 360.0) degrees -= 360.0;
    return Millimetres(degrees);
}

TrackFreeElement Element(const NewTrackOptions& o, int room, double fraction, double lateral, int type)
{
    TrackFreeElement e;
    const Point p = OnRoad(o, room, fraction, lateral);
    e.section = room + 1;
    e.x = p.x; e.y = p.y; e.z = 0.0;
    e.orientation = Heading(o, room, fraction);
    e.factory = 1;
    e.elementType = type;
    return e;
}
}  // namespace

bool MakeRingTrack(const NewTrackOptions& o, TrackModel& model, std::string& error)
{
    model = TrackModel();
    error.clear();
    if (o.rooms < 8 || o.rooms > 96) { error = "a loop needs between 8 and 96 rooms"; return false; }
    if (o.halfWidth < 6.0 || o.halfWidth > 40.0) { error = "the road must be between 12 and 80 metres wide"; return false; }
    if (o.radius < o.halfWidth * 3.0) { error = "the loop is too tight for the road width (radius must be at least three times the half width)"; return false; }
    const double chord = 2.0 * (o.radius - o.halfWidth) * std::sin(kPi / o.rooms);
    if (chord < 24.0) { error = "the rooms would be shorter than 24 metres: use fewer rooms or a larger radius"; return false; }
    // The starting grid (four rows of two) needs room to sit behind the finish line, in room 1.
    const double roomLength = 2.0 * o.radius * std::sin(kPi / o.rooms);
    if (roomLength < 30.0) { error = "the first room is too short for the starting grid: use fewer rooms or a larger radius"; return false; }

    char length[32];
    std::snprintf(length, sizeof(length), "%.1f", 2.0 * kPi * o.radius / 1000.0);
    model.hasHeader = true;
    model.description = "Level: Easy\\nLength: " + std::string(length) + " km\\n" + o.name + ": a wide loop with gentle bends\\n";
    model.background = "NetTarget/mazes/BG-CITY.pcx";

    for (int i = 0; i < o.rooms; ++i) {
        TrackRoom room;
        room.id = i + 1;
        room.floor.height = 0.0;  room.floor.surface = 51;
        room.ceiling.height = 12.0; room.ceiling.surface = 50;
        // Corners as the bundled tracks have them: left edge (inner) ahead, then right edge, going clockwise on the plan.
        const Point a = OnRoad(o, i, 0.0, -o.halfWidth), b = OnRoad(o, i, 1.0, -o.halfWidth);
        const Point c = OnRoad(o, i, 1.0, o.halfWidth), d = OnRoad(o, i, 0.0, o.halfWidth);
        const Point corners[4] = {a, b, c, d};
        const int surfaces[4] = {52, 52, 53, 53};
        for (int k = 0; k < 4; ++k) {
            WallPoint w; w.x = corners[k].x; w.y = corners[k].y; w.factory = 1; w.surface = surfaces[k];
            room.walls.push_back(w);
        }
        model.rooms.push_back(room);
        TrackConnection link;   // this room's end edge (wall 1) joins the next room's start edge (wall 3)
        link.room1 = i + 1; link.wall1 = 1; link.room2 = (i + 1) % o.rooms + 1; link.wall2 = 3;
        model.connections.push_back(link);
    }
    model.hasConnectionList = true;

    // Eight starts in room 1, four rows of two, behind the finish line (which is at 80% of the room).
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 2; ++col) {
            TrackStart s;
            const Point p = OnRoad(o, 0, 0.12 + 0.17 * row + (col == 1 ? 0.07 : 0.0), col == 0 ? -o.halfWidth * 0.35 : o.halfWidth * 0.35);
            s.section = 1; s.x = p.x; s.y = p.y; s.z = 0.0;
            s.orientation = Heading(o, 0, 0.12 + 0.17 * row);
            s.team = 1 + row * 2 + col;
            model.starts.push_back(s);
        }
    }

    model.freeElements.push_back(Element(o, 0, 0.85, 0.0, kFinish));
    model.freeElements.push_back(Element(o, o.rooms / 3, 0.5, 0.0, kCheckpoint1));
    model.freeElements.push_back(Element(o, (2 * o.rooms) / 3, 0.5, 0.0, kCheckpoint2));
    if (o.pickups) {
        model.freeElements.push_back(Element(o, o.rooms / 6, 0.5, -o.halfWidth * 0.4, kPowerUp));
        model.freeElements.push_back(Element(o, o.rooms / 2, 0.5, o.halfWidth * 0.4, kPowerUp));
        model.freeElements.push_back(Element(o, (5 * o.rooms) / 6, 0.5, 0.0, kPowerUp));
        model.freeElements.push_back(Element(o, o.rooms / 4, 0.5, 0.0, kFuel));
        model.freeElements.push_back(Element(o, (3 * o.rooms) / 4, 0.5, 0.0, kSpeedPad));
    }
    return true;
}
}  // namespace HoverNet

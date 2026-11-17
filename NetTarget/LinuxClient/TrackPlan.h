// A plan view of a track for menus: the floor as a lit shape (higher floors lighter, so bridges and ramps show), a bright edge, and the
// pads on it in colour (fuel red, boost orange, finish line gold, power-ups cyan). Built from the loaded level, so it is the real
// track, not a stored picture. Presentation only: it reads a level and never touches a race.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <mutex>

namespace HoverNetPlan
{
// A surface's picture as the game stores it (a floor or wall texture), for a menu that draws the track on the graphics card.
struct Texture
{
    int width = 0, height = 0;
    float mmW = 4000.0f, mmH = 4000.0f;   // how big the picture is in the world, in millimetres
    std::vector<std::uint8_t> rgba;       // width * height * 4, row by row
};
// Reads the picture of a level surface (an MR_SurfaceElement*, passed as void*): set once by the client, which links the code that knows how the game stores them. Returns false for a surface with no picture.
typedef bool (*TextureReader)(const void* surfaceElement, Texture& out);
void SetTextureReader(TextureReader reader);

// Loading a level and drawing from one are not done at the same time by two threads (the menu backdrop draws on its own thread): everything that loads or draws takes this.
std::mutex& LevelLock();

struct Plan
{
    bool ok = false;
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;   // width * height * 4, straight alpha
    int rooms = 0, fuelPads = 0, boostPads = 0, powerUps = 0, finishLines = 0;
    // The shapes themselves, for drawing the track tilted and turning: x and y are in units of the track's longer side, centred on 0; h is the floor height 0..1.
    struct Shape { std::vector<float> xy; float h = 0; int texture = -1; };
    struct Mark { float x = 0, y = 0, h = 0; int kind = 0; };   // kind: 0 fuel, 1 boost, 2 finish line, 3 power-up
    struct Wall { float x0 = 0, y0 = 0, x1 = 0, y1 = 0, h = 0; int texture = -1; };   // an edge of a room with no room beyond it: a rail or wall
    std::vector<Shape> shapes;
    std::vector<Wall> walls;
    std::vector<Texture> textures;    // the pictures the shapes and walls use (only when asked for)
    double spanMm = 1.0, cxMm = 0.0, cyMm = 0.0;   // the longer side of the track and its centre, in millimetres: the shapes' x and y are (world - centre) / span
    float relief = 0;                 // the highest floor above the lowest, in units of the track's longer side
    std::vector<Mark> marks;
    double lengthKm = 0.0;            // the length of the shortest lap-shaped route is not known here: 0 = unknown
};

// Loads `trackPath` (a .trk) as a level and draws it so its longer side is `maxSide` pixels. Returns ok == false when it cannot be loaded.
Plan BuildPlan(const std::string& trackPath, const std::string& trackName, int maxSide, bool withTextures = false);

// The colours the plan uses, so a legend and the in-race map can match.
struct Colours { std::uint8_t fuel[3], boost[3], finish[3], power[3]; };
const Colours& PadColours();
}

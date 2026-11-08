// What the track editor does to a track, as plain functions on a TrackModel: no window, no mouse. The editor's interface calls these and the tests call
// them, so what the tests prove is what a person gets. Every function that can refuse says why in plain words (shown in the editor's status line) and
// leaves the track unchanged when it refuses.
//
// Coordinates are metres on the plan, y up, as in the track source. Orientations are degrees, 0 along +x and growing anticlockwise (checked in the game's
// simulation: a craft started at 91.8 accelerates along +y).
#pragma once

#include "TrackSource.h"

#include <string>

namespace HoverNet
{
struct PlanBounds
{
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool valid = false;
};

enum class SurfaceSlot { Floor, Ceiling, Wall };

PlanBounds ModelBounds(const TrackModel& model);

// True when the point is inside the polygon of the room's walls.
bool PointInRoom(const TrackRoom& room, double x, double y);
// Index in model.rooms of the first room (not a feature) that contains the point, or -1.
int RoomAt(const TrackModel& model, double x, double y);
// The direction of travel through a room in degrees, from its entry edge to its exit edge (four-sided rooms; 0 for other shapes).
double RoomHeading(const TrackRoom& room);

// Starts and elements. A position must be inside a room; the element or start is then in that room (its Section follows it).
bool MoveStart(TrackModel& model, int index, double x, double y, std::string& why);
bool MoveElement(TrackModel& model, int index, double x, double y, std::string& why);
// Both return the new index, or -1 with why set. A new start takes the next team number; a new element faces along the road. Only elements a track may
// place (TrackElementCatalogue.h) can be added.
int AddStart(TrackModel& model, double x, double y, std::string& why);
int AddElement(TrackModel& model, int elementType, double x, double y, std::string& why);
bool DeleteStart(TrackModel& model, int index, std::string& why);
bool DeleteElement(TrackModel& model, int index, std::string& why);
bool SetOrientation(TrackModel& model, bool isStart, int index, double degrees, std::string& why);

// Rooms and features: heights and surfaces. A surface must be one the game knows (a texture, not an element); a ceiling must stay above its floor.
bool SetRoomHeight(TrackModel& model, int roomIndex, SurfaceSlot slot, double height, std::string& why);
bool SetRoomSurface(TrackModel& model, int roomIndex, SurfaceSlot slot, int surfaceId, std::string& why);
// Slides the whole room (its walls) by a vector, with the starts and elements inside it; neighbouring rooms are not changed, so a connection that no
// longer meets is a problem the editor reports (the validator is the authority).
bool MoveWallPoint(TrackModel& model, int roomIndex, int wallIndex, double x, double y, std::string& why);
// Moves a corner together with every other room's corner that sits on the same point, so rooms that share an edge stay joined. All or nothing.
bool MoveSharedCorner(TrackModel& model, int roomIndex, int wallIndex, double x, double y, std::string& why);
}  // namespace HoverNet

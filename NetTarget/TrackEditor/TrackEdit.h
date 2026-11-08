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

// ---- building a road room by room (docs/roadmap-track-editor.md, Phase 2)
// Adds a new room beyond one edge of a room: the shared edge is the new room's entry (its wall 3), and its far edge is `length` metres away, turned by
// `turnDegrees` (positive turns left). Joins the two rooms. Returns the new room's index, or -1 with `why` set. The edge must not already lead to another room.
int ExtendRoad(TrackModel& model, int roomIndex, int wallIndex, double length, double turnDegrees, std::string& why);
// Finds room edges that meet (the same two corners, within `tolerance` metres), makes the corners exactly equal and joins the rooms. Returns how many joins it added.
int JoinTouchingEdges(TrackModel& model, double tolerance = 0.05);
// Rooms are kept numbered 1, 2, 3... in order (tidy, and what the editor writes; the compiler itself looks ids up): renumbers after a room is removed, keeping every reference right.
void RenumberRooms(TrackModel& model);
// Removes a room and the joins that touch it. Refused while a start or element is inside it, or a feature is in it: move or delete those first.
bool DeleteRoom(TrackModel& model, int roomIndex, std::string& why);
// The nearest room edge to a point within `radius` metres (preferring edges that do not yet lead anywhere); false when none is near.
bool NearestEdge(const TrackModel& model, double x, double y, double radius, int& roomIndex, int& wallIndex);
// True when this edge of the room is already joined to another room.
bool EdgeIsJoined(const TrackModel& model, int roomIndex, int wallIndex);
}  // namespace HoverNet

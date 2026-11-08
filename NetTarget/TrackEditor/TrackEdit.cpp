#include "TrackEdit.h"

#include "TrackElementCatalogue.h"

#include <algorithm>
#include <cmath>

namespace HoverNet
{
namespace
{
const double kPi = 3.14159265358979323846;

double Millimetres(double metres)
{
    const double rounded = std::floor(metres * 1000.0 + 0.5) / 1000.0;
    return rounded == 0.0 ? 0.0 : rounded;
}

bool Refuse(std::string& why, const std::string& text)
{
    why = text;
    return false;
}

bool InRange(int index, std::size_t size) { return index >= 0 && static_cast<std::size_t>(index) < size; }
}  // namespace

PlanBounds ModelBounds(const TrackModel& model)
{
    PlanBounds b;
    for (const TrackRoom& room : model.rooms) {
        for (const WallPoint& w : room.walls) {
            if (!b.valid) { b.minX = b.maxX = w.x; b.minY = b.maxY = w.y; b.valid = true; }
            b.minX = std::min(b.minX, w.x); b.maxX = std::max(b.maxX, w.x);
            b.minY = std::min(b.minY, w.y); b.maxY = std::max(b.maxY, w.y);
        }
    }
    return b;
}

bool PointInRoom(const TrackRoom& room, double x, double y)
{
    bool inside = false;
    const std::size_t n = room.walls.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const WallPoint& a = room.walls[i];
        const WallPoint& b = room.walls[j];
        if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) inside = !inside;
    }
    return n >= 3 && inside;
}

int RoomAt(const TrackModel& model, double x, double y)
{
    for (std::size_t i = 0; i < model.rooms.size(); ++i) {
        if (!model.rooms[i].isFeature && PointInRoom(model.rooms[i], x, y)) return static_cast<int>(i);
    }
    return -1;
}

double RoomHeading(const TrackRoom& room)
{
    if (room.walls.size() != 4) return 0.0;
    const WallPoint &a = room.walls[0], &b = room.walls[1], &c = room.walls[2], &d = room.walls[3];
    const double fx = (b.x + c.x) / 2.0 - (d.x + a.x) / 2.0;
    const double fy = (b.y + c.y) / 2.0 - (d.y + a.y) / 2.0;
    double degrees = std::atan2(fy, fx) * 180.0 / kPi;
    if (degrees < 0) degrees += 360.0;
    return Millimetres(degrees);
}

bool MoveStart(TrackModel& model, int index, double x, double y, std::string& why)
{
    if (!InRange(index, model.starts.size())) return Refuse(why, "there is no such start");
    const int room = RoomAt(model, x, y);
    if (room < 0) return Refuse(why, "a start has to be inside a room");
    TrackStart& s = model.starts[static_cast<std::size_t>(index)];
    s.x = Millimetres(x); s.y = Millimetres(y); s.section = model.rooms[static_cast<std::size_t>(room)].id;
    return true;
}

bool MoveElement(TrackModel& model, int index, double x, double y, std::string& why)
{
    if (!InRange(index, model.freeElements.size())) return Refuse(why, "there is no such element");
    const int room = RoomAt(model, x, y);
    if (room < 0) return Refuse(why, "an element has to be inside a room");
    TrackFreeElement& e = model.freeElements[static_cast<std::size_t>(index)];
    e.x = Millimetres(x); e.y = Millimetres(y); e.section = model.rooms[static_cast<std::size_t>(room)].id;
    return true;
}

int AddStart(TrackModel& model, double x, double y, std::string& why)
{
    const int room = RoomAt(model, x, y);
    if (room < 0) { why = "a start has to be inside a room"; return -1; }
    if (model.starts.size() >= 10) { why = "a track can have at most ten starts"; return -1; }
    TrackStart s;
    const TrackRoom& r = model.rooms[static_cast<std::size_t>(room)];
    s.section = r.id; s.x = Millimetres(x); s.y = Millimetres(y); s.z = 0.0;
    s.orientation = RoomHeading(r);
    int team = 0;
    for (const TrackStart& other : model.starts) team = std::max(team, other.team);
    s.team = team + 1;
    model.starts.push_back(s);
    return static_cast<int>(model.starts.size()) - 1;
}

int AddElement(TrackModel& model, int elementType, double x, double y, std::string& why)
{
    const TrackElementInfo* info = FindTrackElement(static_cast<unsigned short>(elementType));
    if (info == nullptr || info->kind != TrackElementKind::PlacedElement) { why = "that is not something a track can place"; return -1; }
    const int room = RoomAt(model, x, y);
    if (room < 0) { why = "an element has to be inside a room"; return -1; }
    // one finish line and one of each checkpoint: a second would make the lap route ambiguous (the lint warns about it too)
    if (elementType >= 202 && elementType <= 204) {
        for (const TrackFreeElement& other : model.freeElements)
            if (other.factory == 1 && other.elementType == elementType) { why = std::string("the track already has a ") + info->name + ": move it instead"; return -1; }
    }
    TrackFreeElement e;
    const TrackRoom& r = model.rooms[static_cast<std::size_t>(room)];
    e.section = r.id; e.x = Millimetres(x); e.y = Millimetres(y); e.z = 0.0;
    e.orientation = RoomHeading(r);
    e.factory = 1; e.elementType = elementType;
    model.freeElements.push_back(e);
    return static_cast<int>(model.freeElements.size()) - 1;
}

bool DeleteStart(TrackModel& model, int index, std::string& why)
{
    if (!InRange(index, model.starts.size())) return Refuse(why, "there is no such start");
    model.starts.erase(model.starts.begin() + index);
    return true;
}

bool DeleteElement(TrackModel& model, int index, std::string& why)
{
    if (!InRange(index, model.freeElements.size())) return Refuse(why, "there is no such element");
    model.freeElements.erase(model.freeElements.begin() + index);
    return true;
}

bool SetOrientation(TrackModel& model, bool isStart, int index, double degrees, std::string& why)
{
    if (!(degrees >= -720.0 && degrees <= 720.0)) return Refuse(why, "an orientation is an angle in degrees");
    while (degrees < 0) degrees += 360.0;
    while (degrees >= 360.0) degrees -= 360.0;
    degrees = Millimetres(degrees);
    if (isStart) { if (!InRange(index, model.starts.size())) return Refuse(why, "there is no such start"); model.starts[static_cast<std::size_t>(index)].orientation = degrees; }
    else { if (!InRange(index, model.freeElements.size())) return Refuse(why, "there is no such element"); model.freeElements[static_cast<std::size_t>(index)].orientation = degrees; }
    return true;
}

bool SetRoomHeight(TrackModel& model, int roomIndex, SurfaceSlot slot, double height, std::string& why)
{
    if (!InRange(roomIndex, model.rooms.size())) return Refuse(why, "there is no such room");
    if (slot == SurfaceSlot::Wall) return Refuse(why, "walls have no height of their own: set the floor and the ceiling");
    if (!(height > -1000.0 && height < 1000.0)) return Refuse(why, "that height is not sensible");
    TrackRoom& r = model.rooms[static_cast<std::size_t>(roomIndex)];
    const double h = Millimetres(height);
    if (slot == SurfaceSlot::Floor && !r.isFeature && h >= r.ceiling.height) return Refuse(why, "the floor must be below the ceiling");
    if (slot == SurfaceSlot::Ceiling && !r.isFeature && h <= r.floor.height) return Refuse(why, "the ceiling must be above the floor");
    (slot == SurfaceSlot::Floor ? r.floor : r.ceiling).height = h;
    return true;
}

bool SetRoomSurface(TrackModel& model, int roomIndex, SurfaceSlot slot, int surfaceId, std::string& why)
{
    if (!InRange(roomIndex, model.rooms.size())) return Refuse(why, "there is no such room");
    const TrackElementInfo* info = FindTrackElement(static_cast<unsigned short>(surfaceId));
    if (info == nullptr || info->kind != TrackElementKind::Surface) return Refuse(why, "that is not a surface the game has");
    TrackRoom& r = model.rooms[static_cast<std::size_t>(roomIndex)];
    if (slot == SurfaceSlot::Floor) { r.floor.factory = 1; r.floor.surface = surfaceId; }
    else if (slot == SurfaceSlot::Ceiling) { r.ceiling.factory = 1; r.ceiling.surface = surfaceId; }
    else for (WallPoint& w : r.walls) { w.factory = 1; w.surface = surfaceId; }
    return true;
}

bool MoveWallPoint(TrackModel& model, int roomIndex, int wallIndex, double x, double y, std::string& why)
{
    if (!InRange(roomIndex, model.rooms.size())) return Refuse(why, "there is no such room");
    TrackRoom& r = model.rooms[static_cast<std::size_t>(roomIndex)];
    if (!InRange(wallIndex, r.walls.size())) return Refuse(why, "there is no such corner");
    if (!(std::fabs(x) < 100000.0 && std::fabs(y) < 100000.0)) return Refuse(why, "that is too far from the middle of the plan");
    const WallPoint keep = r.walls[static_cast<std::size_t>(wallIndex)];
    r.walls[static_cast<std::size_t>(wallIndex)].x = Millimetres(x);
    r.walls[static_cast<std::size_t>(wallIndex)].y = Millimetres(y);
    // the room must stay a proper shape: no corner on top of another
    for (std::size_t i = 0; i < r.walls.size(); ++i)
        for (std::size_t j = i + 1; j < r.walls.size(); ++j)
            if (std::fabs(r.walls[i].x - r.walls[j].x) < 0.01 && std::fabs(r.walls[i].y - r.walls[j].y) < 0.01) {
                r.walls[static_cast<std::size_t>(wallIndex)] = keep;
                return Refuse(why, "two corners of a room cannot be in the same place");
            }
    return true;
}

bool MoveSharedCorner(TrackModel& model, int roomIndex, int wallIndex, double x, double y, std::string& why)
{
    if (!InRange(roomIndex, model.rooms.size())) return Refuse(why, "there is no such room");
    const TrackRoom& room = model.rooms[static_cast<std::size_t>(roomIndex)];
    if (!InRange(wallIndex, room.walls.size())) return Refuse(why, "there is no such corner");
    const double ox = room.walls[static_cast<std::size_t>(wallIndex)].x, oy = room.walls[static_cast<std::size_t>(wallIndex)].y;
    TrackModel work = model;
    for (std::size_t r = 0; r < work.rooms.size(); ++r) {
        for (std::size_t w = 0; w < work.rooms[r].walls.size(); ++w) {
            const WallPoint& p = model.rooms[r].walls[w];
            if (std::fabs(p.x - ox) < 0.01 && std::fabs(p.y - oy) < 0.01) {
                if (!MoveWallPoint(work, static_cast<int>(r), static_cast<int>(w), x, y, why)) return false;
            }
        }
    }
    model = work;
    return true;
}
}  // namespace HoverNet

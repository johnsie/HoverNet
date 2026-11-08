#include "TrackEdit.h"

#include "TrackElementCatalogue.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

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

namespace
{
double SignedArea(const TrackRoom& r)   // positive when the corners run anticlockwise (y up)
{
    double a = 0.0;
    for (std::size_t i = 0; i < r.walls.size(); ++i) {
        const WallPoint& p = r.walls[i];
        const WallPoint& q = r.walls[(i + 1) % r.walls.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a / 2.0;
}
}  // namespace

bool EdgeIsJoined(const TrackModel& model, int roomIndex, int wallIndex)
{
    if (!InRange(roomIndex, model.rooms.size())) return false;
    const int id = model.rooms[static_cast<std::size_t>(roomIndex)].id;
    for (const TrackConnection& c : model.connections)
        if ((c.room1 == id && c.wall1 == wallIndex) || (c.room2 == id && c.wall2 == wallIndex)) return true;
    return false;
}

int ExtendRoad(TrackModel& model, int roomIndex, int wallIndex, double length, double turnDegrees, std::string& why)
{
    auto fail = [&](const std::string& text) { why = text; return -1; };
    if (!InRange(roomIndex, model.rooms.size())) return fail("there is no such room");
    const TrackRoom src = model.rooms[static_cast<std::size_t>(roomIndex)];
    if (src.isFeature) return fail("the road is extended from a room, not from a feature");
    if (src.walls.size() < 3 || !InRange(wallIndex, src.walls.size())) return fail("there is no such edge");
    if (!(length >= 12.0 && length <= 200.0)) return fail("a room is between 12 and 200 metres long");
    if (!(turnDegrees >= -90.0 && turnDegrees <= 90.0)) return fail("a room can turn at most 90 degrees");
    if (EdgeIsJoined(model, roomIndex, wallIndex)) return fail("that edge already leads to another room");
    const WallPoint P = src.walls[static_cast<std::size_t>(wallIndex)];
    const WallPoint Q = src.walls[(static_cast<std::size_t>(wallIndex) + 1) % src.walls.size()];
    const double dx = Q.x - P.x, dy = Q.y - P.y, span = std::hypot(dx, dy);
    if (span < 6.0) return fail("that edge is too short to build a road from (under 6 metres)");
    if (span > 120.0) return fail("that edge is too wide to build a road from (over 120 metres)");
    // the way out of the room: to the left of P->Q for corners that run clockwise, to the right for anticlockwise
    const double side = SignedArea(src) < 0 ? 1.0 : -1.0;
    const double nx = side * -dy / span, ny = side * dx / span;
    const double t = turnDegrees * kPi / 180.0;
    auto rotate = [](double x, double y, double a) { return std::make_pair(x * std::cos(a) - y * std::sin(a), x * std::sin(a) + y * std::cos(a)); };
    const double mx = (P.x + Q.x) / 2.0, my = (P.y + Q.y) / 2.0;
    const std::pair<double, double> way = rotate(nx, ny, t / 2.0);          // the bisector between the way in and the way out
    const double mx2 = mx + way.first * length, my2 = my + way.second * length;
    const std::pair<double, double> half = rotate((Q.x - P.x) / 2.0, (Q.y - P.y) / 2.0, t);
    TrackRoom room;
    room.isFeature = false;
    room.id = 0;
    for (const TrackRoom& r : model.rooms) if (!r.isFeature) room.id = std::max(room.id, r.id);
    room.id += 1;
    room.floor = src.floor; room.ceiling = src.ceiling;
    WallPoint w[4] = {P, P, Q, Q};
    w[1].x = Millimetres(mx2 - half.first);  w[1].y = Millimetres(my2 - half.second);
    w[2].x = Millimetres(mx2 + half.first);  w[2].y = Millimetres(my2 + half.second);
    const bool four = src.walls.size() == 4;
    const int surfaces[4] = {four ? src.walls[0].surface : 52, four ? src.walls[1].surface : 52, four ? src.walls[2].surface : 53, four ? src.walls[3].surface : 53};
    for (int i = 0; i < 4; ++i) { w[i].factory = 1; w[i].surface = surfaces[i]; room.walls.push_back(w[i]); }
    model.rooms.push_back(room);
    TrackConnection join;
    join.room1 = src.id; join.wall1 = wallIndex; join.room2 = room.id; join.wall2 = 3;
    model.connections.push_back(join);
    // the new far edge may land on another room's edge (closing a loop): join those too
    JoinTouchingEdges(model);
    return static_cast<int>(model.rooms.size()) - 1;
}

int JoinTouchingEdges(TrackModel& model, double tolerance)
{
    int added = 0;
    for (std::size_t i = 0; i < model.rooms.size(); ++i) {
        if (model.rooms[i].isFeature) continue;
        for (std::size_t j = i + 1; j < model.rooms.size(); ++j) {
            if (model.rooms[j].isFeature) continue;
            const std::size_t ni = model.rooms[i].walls.size(), nj = model.rooms[j].walls.size();
            for (std::size_t a = 0; a < ni; ++a) {
                const WallPoint pa = model.rooms[i].walls[a], qa = model.rooms[i].walls[(a + 1) % ni];
                for (std::size_t b = 0; b < nj; ++b) {
                    WallPoint& pb = model.rooms[j].walls[b];
                    WallPoint& qb = model.rooms[j].walls[(b + 1) % nj];
                    // two rooms share an edge when it runs the opposite way in each (both polygons run the same way round)
                    if (std::hypot(pa.x - qb.x, pa.y - qb.y) <= tolerance && std::hypot(qa.x - pb.x, qa.y - pb.y) <= tolerance) {
                        if (EdgeIsJoined(model, static_cast<int>(i), static_cast<int>(a)) || EdgeIsJoined(model, static_cast<int>(j), static_cast<int>(b))) continue;
                        qb.x = pa.x; qb.y = pa.y; pb.x = qa.x; pb.y = qa.y;   // exactly equal, so the compiler sees one edge
                        TrackConnection c;
                        c.room1 = model.rooms[i].id; c.wall1 = static_cast<int>(a); c.room2 = model.rooms[j].id; c.wall2 = static_cast<int>(b);
                        model.connections.push_back(c);
                        ++added;
                    }
                }
            }
        }
    }
    return added;
}

void RenumberRooms(TrackModel& model)
{
    std::map<int, int> room, feature;
    int nextRoom = 1, nextFeature = 1;
    for (TrackRoom& r : model.rooms) (r.isFeature ? feature : room)[r.id] = r.isFeature ? nextFeature++ : nextRoom++;
    for (TrackRoom& r : model.rooms) {
        if (r.isFeature) r.parent = room.count(r.parent) ? room[r.parent] : r.parent;
        r.id = (r.isFeature ? feature : room)[r.id];
    }
    for (TrackStart& s : model.starts) if (room.count(s.section)) s.section = room[s.section];
    for (TrackFreeElement& e : model.freeElements) if (room.count(e.section)) e.section = room[e.section];
    for (TrackConnection& c : model.connections) {
        if (room.count(c.room1)) c.room1 = room[c.room1];
        if (room.count(c.room2)) c.room2 = room[c.room2];
    }
}

bool DeleteRoom(TrackModel& model, int roomIndex, std::string& why)
{
    if (!InRange(roomIndex, model.rooms.size())) return Refuse(why, "there is no such room");
    const TrackRoom r = model.rooms[static_cast<std::size_t>(roomIndex)];
    if (r.isFeature) { model.rooms.erase(model.rooms.begin() + roomIndex); RenumberRooms(model); return true; }
    int inside = 0;
    for (const TrackStart& s : model.starts) inside += s.section == r.id;
    for (const TrackFreeElement& e : model.freeElements) inside += e.section == r.id;
    if (inside > 0) return Refuse(why, "room " + std::to_string(r.id) + " still has " + std::to_string(inside) + " start(s) or element(s) in it: move or delete them first");
    if (model.CountRooms() <= 1) return Refuse(why, "a track needs at least one room");
    for (const TrackRoom& f : model.rooms) if (f.isFeature && f.parent == r.id) return Refuse(why, "room " + std::to_string(r.id) + " has a feature in it: delete the feature first");
    model.rooms.erase(model.rooms.begin() + roomIndex);
    std::vector<TrackConnection> kept;
    for (const TrackConnection& c : model.connections) if (c.room1 != r.id && c.room2 != r.id) kept.push_back(c);
    model.connections = kept;
    RenumberRooms(model);
    return true;
}

bool NearestEdge(const TrackModel& model, double x, double y, double radius, int& roomIndex, int& wallIndex)
{
    double best = radius + 1.0;
    bool found = false;
    for (std::size_t i = 0; i < model.rooms.size(); ++i) {
        const TrackRoom& r = model.rooms[i];
        if (r.isFeature) continue;
        for (std::size_t k = 0; k < r.walls.size(); ++k) {
            const WallPoint& p = r.walls[k];
            const WallPoint& q = r.walls[(k + 1) % r.walls.size()];
            const double ex = q.x - p.x, ey = q.y - p.y, len2 = ex * ex + ey * ey;
            double u = len2 > 0 ? ((x - p.x) * ex + (y - p.y) * ey) / len2 : 0.0;
            u = std::max(0.0, std::min(1.0, u));
            double d = std::hypot(x - (p.x + u * ex), y - (p.y + u * ey));
            if (EdgeIsJoined(model, static_cast<int>(i), static_cast<int>(k))) d += radius * 0.5;   // an open edge is the likelier target
            if (d < best && d <= radius + radius * 0.5) { best = d; roomIndex = static_cast<int>(i); wallIndex = static_cast<int>(k); found = true; }
        }
    }
    return found;
}
}  // namespace HoverNet

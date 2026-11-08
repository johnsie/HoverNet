#include "EditorSession.h"
#include "HoverCadImport.h"

#include "TrackElementCatalogue.h"

#include <cmath>
#include <fstream>
#include <iterator>

namespace HoverNet
{
namespace
{
const std::size_t kHistory = 200;
}

EditorSession::EditorSession() { Refresh(); }

void EditorSession::Refresh()
{
    problems = model.Problems();
}

void EditorSession::Checkpoint()
{
    mUndo.push_back(model);
    if (mUndo.size() > kHistory) mUndo.erase(mUndo.begin());
    mRedo.clear();
}

void EditorSession::Changed(const std::string& message)
{
    dirty = true;
    status = message;
    Refresh();
}

bool EditorSession::NewLoop(const NewTrackOptions& options)
{
    TrackModel fresh;
    std::string error;
    if (!MakeRingTrack(options, fresh, error)) { status = error; return false; }
    model = fresh;
    path.clear(); dirty = true; selection = Selection(); mUndo.clear(); mRedo.clear(); mDragging = false;
    status = "A new track: " + options.name + ". It is ready to race: move things, change the road, then check and save it.";
    Refresh();
    return true;
}

bool EditorSession::NewStartRoom(const std::string& name)
{
    TrackModel fresh;
    std::string error;
    if (!MakeStartRoomTrack(name, fresh, error)) { status = error; return false; }
    model = fresh;
    path.clear(); dirty = true; selection = Selection(); mUndo.clear(); mRedo.clear(); mDragging = false;
    status = "A start room for " + name + ". Use the Extend the road tool to build on from its open end, then place the two checkpoints.";
    Refresh();
    return true;
}

bool EditorSession::Open(const std::string& filename)
{
    std::ifstream in(filename.c_str(), std::ios::binary);
    if (!in) { status = "cannot open " + filename; return false; }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    TrackModel opened;
    std::string error;
    bool imported = false;
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    if (LooksLikeHoverCad(bytes)) {   // a HoverCad project: converted, and offered for saving under a new name (the original is never overwritten)
        std::vector<std::string> notes;
        if (!ImportHoverCad(bytes, opened, error, notes)) { status = filename + ": " + error; return false; }
        imported = true;
    }
    else if (!TrackModel::FromText(TrackText::Parse(text), opened, error)) { status = filename + ": " + error; return false; }
    model = opened;
    path = imported ? std::string() : filename; dirty = imported; selection = Selection(); mUndo.clear(); mRedo.clear(); mDragging = false;
    status = imported ? "Imported HoverCad project " + filename + " (save it as a new track)" : "Opened " + filename;
    Refresh();
    return true;
}

bool EditorSession::OpenText(const std::string& text, const std::string& label)
{
    TrackModel opened;
    std::string error;
    if (!TrackModel::FromText(TrackText::Parse(text), opened, error) || opened.rooms.empty()) { status = "that is not a track I can read" + (error.empty() ? std::string() : ": " + error); return false; }
    model = opened;
    path.clear(); dirty = true; selection = Selection(); mUndo.clear(); mRedo.clear(); mDragging = false;
    status = label;
    Refresh();
    return true;
}

bool EditorSession::SaveAs(const std::string& filename)
{
    std::ofstream out(filename.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) { status = "cannot write " + filename; return false; }
    out << model.ToText();
    out.close();
    if (!out) { status = "could not finish writing " + filename; return false; }
    path = filename; dirty = false;
    status = "Saved " + filename;
    return true;
}

bool EditorSession::Save()
{
    if (path.empty()) { status = "choose a file name to save to"; return false; }
    return SaveAs(path);
}

bool EditorSession::Undo()
{
    if (mUndo.empty()) { status = "nothing to undo"; return false; }
    mRedo.push_back(model);
    model = mUndo.back(); mUndo.pop_back();
    selection = Selection(); dirty = true; status = "Undone"; Refresh();
    return true;
}

bool EditorSession::Redo()
{
    if (mRedo.empty()) { status = "nothing to redo"; return false; }
    mUndo.push_back(model);
    model = mRedo.back(); mRedo.pop_back();
    selection = Selection(); dirty = true; status = "Redone"; Refresh();
    return true;
}

Selection EditorSession::HitTest(double x, double y, double radius) const
{
    auto near = [&](double px, double py) { return std::hypot(px - x, py - y) <= radius; };
    Selection best;
    double bestDistance = radius + 1;
    for (std::size_t i = 0; i < model.freeElements.size(); ++i) {
        const double d = std::hypot(model.freeElements[i].x - x, model.freeElements[i].y - y);
        if (near(model.freeElements[i].x, model.freeElements[i].y) && d < bestDistance) { best = Selection{Selection::Element, static_cast<int>(i), -1}; bestDistance = d; }
    }
    for (std::size_t i = 0; i < model.starts.size(); ++i) {
        const double d = std::hypot(model.starts[i].x - x, model.starts[i].y - y);
        if (near(model.starts[i].x, model.starts[i].y) && d < bestDistance) { best = Selection{Selection::Start, static_cast<int>(i), -1}; bestDistance = d; }
    }
    if (best.kind != Selection::None) return best;
    // the corners of the selected room can be picked up
    if (selection.kind == Selection::Room || selection.kind == Selection::Corner) {
        const TrackRoom& r = model.rooms[static_cast<std::size_t>(selection.index)];
        for (std::size_t c = 0; c < r.walls.size(); ++c)
            if (near(r.walls[c].x, r.walls[c].y)) return Selection{Selection::Corner, selection.index, static_cast<int>(c)};
    }
    const int room = RoomAt(model, x, y);
    if (room >= 0) return Selection{Selection::Room, room, -1};
    return Selection();
}

bool EditorSession::BeginDrag(double x, double y, double radius)
{
    selection = HitTest(x, y, radius);
    mDragging = false; mMoved = false;
    if (selection.kind == Selection::Start || selection.kind == Selection::Element || selection.kind == Selection::Corner) {
        mBeforeDrag = model;
        mDragging = true;
        return true;
    }
    return selection.kind != Selection::None;
}

bool EditorSession::Drag(double x, double y)
{
    if (!mDragging) return false;
    std::string why;
    bool ok = false;
    if (selection.kind == Selection::Start) ok = MoveStart(model, selection.index, x, y, why);
    else if (selection.kind == Selection::Element) ok = MoveElement(model, selection.index, x, y, why);
    else if (selection.kind == Selection::Corner) ok = MoveSharedCorner(model, selection.index, selection.corner, x, y, why);
    if (!ok) { status = why; return false; }
    mMoved = true;
    dirty = true;
    Refresh();
    return true;
}

void EditorSession::EndDrag()
{
    if (mDragging && mMoved) {
        mUndo.push_back(mBeforeDrag);
        if (mUndo.size() > kHistory) mUndo.erase(mUndo.begin());
        mRedo.clear();
        status = "Moved";
    }
    mDragging = false; mMoved = false;
}

bool EditorSession::PlaceElement(int elementType, double x, double y)
{
    std::string why;
    TrackModel keep = model;
    const int index = AddElement(model, elementType, x, y, why);
    if (index < 0) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    selection = Selection{Selection::Element, index, -1};
    const TrackElementInfo* info = FindTrackElement(static_cast<unsigned short>(elementType));
    Changed(std::string("Placed ") + (info ? info->name : "an element"));
    return true;
}

bool EditorSession::PlaceStart(double x, double y)
{
    std::string why;
    TrackModel keep = model;
    const int index = AddStart(model, x, y, why);
    if (index < 0) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    selection = Selection{Selection::Start, index, -1};
    Changed("Placed a start");
    return true;
}

bool EditorSession::DeleteSelected()
{
    std::string why;
    TrackModel keep = model;
    bool ok = false;
    if (selection.kind == Selection::Element) ok = DeleteElement(model, selection.index, why);
    else if (selection.kind == Selection::Start) ok = DeleteStart(model, selection.index, why);
    else if (selection.kind == Selection::Room || selection.kind == Selection::Corner) ok = DeleteRoom(model, selection.index, why);
    else { status = "select something to delete: an element, a start or a room"; return false; }
    if (!ok) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    selection = Selection();
    Changed("Deleted");
    return true;
}

bool EditorSession::SetHeight(SurfaceSlot slot, double height)
{
    if (selection.kind != Selection::Room && selection.kind != Selection::Corner) { status = "select a room first"; return false; }
    std::string why;
    TrackModel keep = model;
    if (!SetRoomHeight(model, selection.index, slot, height, why)) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    Changed(slot == SurfaceSlot::Floor ? "Floor height changed" : "Ceiling height changed");
    return true;
}

bool EditorSession::SetSurface(SurfaceSlot slot, int surfaceId)
{
    if (selection.kind != Selection::Room && selection.kind != Selection::Corner) { status = "select a room first"; return false; }
    std::string why;
    TrackModel keep = model;
    if (!SetRoomSurface(model, selection.index, slot, surfaceId, why)) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    Changed("Surface changed");
    return true;
}

bool EditorSession::SetOrientationOfSelected(double degrees)
{
    if (selection.kind != Selection::Start && selection.kind != Selection::Element) { status = "select a start or an element first"; return false; }
    std::string why;
    TrackModel keep = model;
    if (!SetOrientation(model, selection.kind == Selection::Start, selection.index, degrees, why)) { status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    Changed("Turned");
    return true;
}

bool EditorSession::ExtendRoadAt(double x, double y, double radius, double length, double turn)
{
    int room = -1, wall = -1;
    if (!NearestEdge(model, x, y, radius, room, wall)) { status = "click close to an edge of a room to build the road from it"; return false; }
    std::string why;
    TrackModel keep = model;
    const int created = ExtendRoad(model, room, wall, length, turn, why);
    if (created < 0) { model = keep; status = why; return false; }
    mUndo.push_back(keep); mRedo.clear();
    selection = Selection{Selection::Room, created, -1};
    Changed("Added room " + std::to_string(model.rooms[static_cast<std::size_t>(created)].id) + " to the road");
    return true;
}

bool EditorSession::PreviewExtend(double x, double y, double radius, double length, double turn, std::vector<WallPoint>& polygon, int& roomIndex, int& wallIndex, std::string& reason) const
{
    if (!NearestEdge(model, x, y, radius, roomIndex, wallIndex)) { reason.clear(); return false; }
    TrackModel scratch = model;
    const int created = ExtendRoad(scratch, roomIndex, wallIndex, length, turn, reason);
    if (created < 0) return false;
    polygon = scratch.rooms[static_cast<std::size_t>(created)].walls;
    return true;
}
}  // namespace HoverNet

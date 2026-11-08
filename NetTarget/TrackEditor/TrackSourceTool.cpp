// Command-line face of the track editor core, and its self-test (no arguments).
//   HoverNetTrackSource                         run the self-test
//   HoverNetTrackSource check FILE [--strict]   FILE must survive Parse/Write unchanged and read into the model; prints what it found
//                                               (--strict also fails when the model reports problems)
//   HoverNetTrackSource new ring OUT [--name=N] [--rooms=N] [--radius=M] [--width=M] [--plain]   make a new track from a template (no pickups with --plain)
//   HoverNetTrackSource canonical IN OUT        write the model's canonical text of IN to OUT (scripts/test-track-source-roundtrip.sh compiles both)
#include "EditorSession.h"
#include "TrackEdit.h"
#include "TrackSource.h"
#include "TrackTemplates.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace HoverNet;

namespace
{
bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int failures = 0;
void Expect(bool condition, const char* what)
{
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

int SelfTest()
{
    // Lossless layer: odd line endings, comments, mixed case, a last line with no ending, a section that starts mid-line (not a section).
    const std::string odd = "; leading comment\r\n[header]\r\nDESCRIPTION=Hi # note\r\n\r\n [Room]\nId=1\r[Room]\nId=2";
    Expect(TrackText::Parse(odd).Write() == odd, "TrackText keeps every byte, whatever the line endings");
    Expect(TrackText::Parse("").Write().empty(), "an empty file stays empty");
    Expect(TrackText::SectionName("[Room] ; x") == "Room", "section name ignores a comment after it");
    Expect(TrackText::SectionName(" [Room]").empty(), "a section must start in column 0, as the compiler requires");
    Expect(TrackText::IsBlankOrComment("   ; just a comment") && TrackText::IsBlankOrComment("") && !TrackText::IsBlankOrComment("Id=1"), "blank and comment lines");

    // Model: read everything, keep what it does not know, write the canonical layout back.
    const std::string src =
        "[Editor]\nView=1, 2\n\n"
        "[HEADER]\nDescription=Level: Easy\\nShort\nBackground=NetTarget/mazes/BG-CITY.pcx\n\n"
        "[room]\nid=1\nFLOOR=0.000, 1, 51\nceiling=12.000, 1, 50\nwall=0.000, 0.000, 1, 52\nwall=10.000, 0.000, 1, 53\nwall=10.000, 10.000, 1, 52\nnote=kept\n\n"
        "[Feature]\nId=1\nParent=1\nfloor=-0.050, 1, 51\nceiling=0.000, 1, 67\nwall=1, 1, 1, 58\nwall=2, 1, 1, 58\nwall=2, 2, 1, 58\n\n"
        "[Initial_Position]\nSection=1\nPosition=1.150, -1.903, 0.000\nOrientation=28.000\nTeam=1\n\n"
        "[Free_Element]\nSection=1\nPosition=2.000, 3.000, 0.000\nOrientation=0.000\nElement_Type=1, 202\n\n"
        "[Connection_List]\n1, 1, 1, 2\n";
    TrackModel model;
    std::string error;
    Expect(TrackModel::FromText(TrackText::Parse(src), model, error), "the model reads the sample");
    Expect(model.CountRooms() == 1 && model.CountFeatures() == 1 && model.starts.size() == 1 && model.freeElements.size() == 1 && model.connections.size() == 1, "the sample's parts are all found");
    Expect(model.description == "Level: Easy\\nShort", "the description keeps its backslash-n as written");
    Expect(model.rooms[0].other.size() == 1 && model.rooms[0].other[0].first == "note", "an attribute the model does not know is kept");
    Expect(model.unknownSections.size() == 1, "the [Editor] section is kept");
    Expect(model.Problems().empty(), "the sample has no problems");
    const std::string canonical = model.ToText();
    TrackModel again;
    Expect(TrackModel::FromText(TrackText::Parse(canonical), again, error) && again.ToText() == canonical, "the canonical text reads back and writes the same");
    Expect(canonical.find("[Editor]\nView=1, 2") != std::string::npos, "the [Editor] section survives into the canonical text");

    // Numbers that three decimals cannot hold are written with enough digits to read back identically.
    model.rooms[0].walls[0].x = 0.1 + 0.2;
    TrackModel precise;
    Expect(TrackModel::FromText(TrackText::Parse(model.ToText()), precise, error) && precise.rooms[0].walls[0].x == 0.1 + 0.2, "a number needing more than three decimals is not rounded");

    // Problems the editor can see without compiling.
    TrackModel bad = model;
    bad.rooms[0].floor.surface = 999;
    bad.rooms[1].parent = 77;
    bad.starts[0].section = 5;
    bad.freeElements[0].elementType = 150;   // a missile: the game creates it, a track cannot place it
    bad.connections[0].room2 = 9;
    const std::vector<std::string> problems = bad.Problems();
    Expect(problems.size() == 5, "five planted problems are found");
    for (const std::string& p : problems) std::printf("  (planted) %s\n", p.c_str());

    // Editing operations (TrackEdit.h): what the editor does, tested here without a window.
    {
        TrackModel ring;
        NewTrackOptions options;
        std::string e2;
        Expect(MakeRingTrack(options, ring, e2), "the loop template makes a track to edit");
        std::string why;
        const PlanBounds bounds = ModelBounds(ring);
        Expect(bounds.valid && bounds.maxX > 140 && bounds.minX < -140, "the plan's bounds cover the loop");
        const TrackFreeElement finish = ring.freeElements[0];
        Expect(RoomAt(ring, finish.x, finish.y) == 0 && RoomAt(ring, 0, 0) == -1, "a point is found in its room, and the hole in the middle is in none");
        Expect(std::fabs(RoomHeading(ring.rooms[0]) - 90.0) < 8.0, "the first room's heading is about 90 degrees (up the right-hand side of an anticlockwise loop)");
        // moving
        const int room5 = 5;
        const WallPoint& w = ring.rooms[room5].walls[0];
        const double mx = (w.x + ring.rooms[room5].walls[2].x) / 2, my = (w.y + ring.rooms[room5].walls[2].y) / 2;
        Expect(MoveElement(ring, 0, mx, my, why) && ring.freeElements[0].section == ring.rooms[room5].id, "moving an element into another room changes its room");
        TrackModel before = ring;
        Expect(!MoveElement(ring, 0, 0, 0, why) && !why.empty() && ring.freeElements[0].x == before.freeElements[0].x, "an element cannot be dropped outside every room, and stays put");
        Expect(MoveStart(ring, 0, mx, my, why) && ring.starts[0].section == ring.rooms[room5].id, "a start moves between rooms too");
        // adding and deleting
        const std::size_t elements = ring.freeElements.size();
        Expect(AddElement(ring, 152, mx, my, why) == static_cast<int>(elements) && ring.freeElements.size() == elements + 1, "a power-up can be added");
        Expect(AddElement(ring, 202, mx, my, why) < 0 && why.find("already has") != std::string::npos, "a second finish line is refused, and the reason says what to do");
        Expect(AddElement(ring, 150, mx, my, why) < 0, "a missile (something only the game creates) cannot be placed");
        Expect(AddElement(ring, 999, mx, my, why) < 0, "an element the game does not know cannot be placed");
        Expect(AddStart(ring, mx, my, why) == 8 && ring.starts.back().team == 9 && std::fabs(ring.starts.back().orientation - RoomHeading(ring.rooms[room5])) < 0.01, "a start added in a room faces along the road, and takes the next team");
        Expect(DeleteElement(ring, static_cast<int>(ring.freeElements.size()) - 1, why) && ring.freeElements.size() == elements && !DeleteElement(ring, 99, why), "an element can be deleted; a missing one cannot");
        // heights and surfaces
        Expect(SetRoomHeight(ring, 2, SurfaceSlot::Ceiling, 15.0, why) && ring.rooms[2].ceiling.height == 15.0, "a ceiling can be raised");
        Expect(!SetRoomHeight(ring, 2, SurfaceSlot::Ceiling, -1.0, why) && why.find("above the floor") != std::string::npos, "a ceiling below the floor is refused");
        Expect(!SetRoomSurface(ring, 2, SurfaceSlot::Floor, 152, why) && SetRoomSurface(ring, 2, SurfaceSlot::Floor, 63, why) && ring.rooms[2].floor.surface == 63, "a floor takes a surface, never an element");
        Expect(SetRoomSurface(ring, 2, SurfaceSlot::Wall, 58, why) && ring.rooms[2].walls[3].surface == 58, "all of a room's walls can be re-surfaced");
        Expect(MoveWallPoint(ring, 3, 0, ring.rooms[3].walls[0].x + 1.0, ring.rooms[3].walls[0].y, why), "a corner can be moved");
        Expect(!MoveWallPoint(ring, 3, 0, ring.rooms[3].walls[1].x, ring.rooms[3].walls[1].y, why) && why.find("same place") != std::string::npos, "a corner cannot go on top of another");
        Expect(SetOrientation(ring, true, 0, -90.0, why) && ring.starts[0].orientation == 270.0, "an orientation is kept between 0 and 360");
        // and the edited track is still a track the compiler reads
        TrackModel again;
        Expect(TrackModel::FromText(TrackText::Parse(ring.ToText()), again, e2) && again.rooms.size() == ring.rooms.size() && again.freeElements.size() == ring.freeElements.size(), "an edited track writes and reads back");
    }

    // The editor's session (EditorSession.h): the same steps a person takes with the mouse, without a window.
    {
        EditorSession ed;
        NewTrackOptions options;
        options.name = "Session Loop";
        Expect(ed.NewLoop(options) && ed.dirty && ed.problems.empty() && !ed.CanUndo(), "a new loop starts clean, unsaved and with nothing to undo");
        const TrackFreeElement finish = ed.model.freeElements[0];
        Expect(ed.HitTest(finish.x, finish.y, 2.0).kind == Selection::Element && ed.HitTest(finish.x, finish.y, 2.0).index == 0, "the finish line is found under the pointer");
        Expect(ed.HitTest(0, 0, 1.0).kind == Selection::None, "the empty middle of the loop selects nothing");
        // drag the finish line down the road
        Expect(ed.BeginDrag(finish.x, finish.y, 2.0) && ed.Dragging(), "pressing on the finish line starts moving it");
        const TrackRoom& next = ed.model.rooms[1];
        const double tx = (next.walls[0].x + next.walls[2].x) / 2, ty = (next.walls[0].y + next.walls[2].y) / 2;
        Expect(ed.Drag(tx, ty) && ed.Drag(tx + 0.5, ty), "dragging moves it");
        ed.EndDrag();
        Expect(ed.model.freeElements[0].section == 2 && ed.CanUndo(), "it is now in room 2 and the move is one undo step");
        Expect(ed.BeginDrag(0, 0, 1.0) == false && !ed.Dragging(), "pressing on nothing moves nothing");
        // a drag outside every room is refused and the thing stays
        ed.BeginDrag(tx + 0.5, ty, 2.0);
        const double keepX = ed.model.freeElements[0].x;
        Expect(!ed.Drag(0, 0) && ed.model.freeElements[0].x == keepX && !ed.status.empty(), "dragging outside every room is refused, with a reason");
        ed.EndDrag();
        Expect(ed.Undo() && ed.model.freeElements[0].section == 1 && ed.CanRedo(), "undo puts the finish line back");
        Expect(ed.Redo() && ed.model.freeElements[0].section == 2, "redo moves it again");
        // pressing and releasing without moving leaves no undo step
        EditorSession quiet;
        quiet.NewLoop(options);
        quiet.BeginDrag(finish.x, finish.y, 2.0);
        quiet.EndDrag();
        Expect(!quiet.CanUndo(), "a click that does not move anything is not an undo step");
        // placing, deleting, properties
        Expect(ed.PlaceElement(152, tx, ty) && ed.selection.kind == Selection::Element, "placing a power-up selects it");
        Expect(!ed.PlaceElement(203, tx, ty) && ed.status.find("already has") != std::string::npos, "a second checkpoint is refused with a reason");
        Expect(ed.SetOrientationOfSelected(45.0) && ed.model.freeElements.back().orientation == 45.0, "the selected element can be turned");
        Expect(ed.DeleteSelected() && ed.selection.kind == Selection::None, "the selected element can be deleted");
        ed.Select(Selection{Selection::Room, 3, -1});
        Expect(ed.SetHeight(SurfaceSlot::Ceiling, 14.0) && ed.model.rooms[3].ceiling.height == 14.0, "the selected room's ceiling can be raised");
        Expect(ed.SetSurface(SurfaceSlot::Floor, 63) && ed.model.rooms[3].floor.surface == 63, "and its floor re-surfaced");
        Expect(ed.Undo() && ed.model.rooms[3].floor.surface == 51 && ed.Undo() && ed.model.rooms[3].ceiling.height == 12.0, "each property change undoes on its own");
        // a corner shared by two rooms moves with both
        ed.Select(Selection{Selection::Room, 4, -1});
        const WallPoint corner = ed.model.rooms[4].walls[1];
        const Selection hit = ed.HitTest(corner.x, corner.y, 1.0);
        Expect(hit.kind == Selection::Corner && hit.index == 4 && hit.corner == 1, "a selected room's corners can be picked up");
        ed.BeginDrag(corner.x, corner.y, 1.0);
        Expect(ed.Drag(corner.x + 2.0, corner.y), "a corner can be dragged");
        ed.EndDrag();
        Expect(ed.model.rooms[5].walls[0].x == ed.model.rooms[4].walls[1].x && ed.model.rooms[5].walls[0].y == ed.model.rooms[4].walls[1].y, "the neighbouring room's corner moved with it, so the rooms stay joined");
        // save and open
        const std::string file = "/tmp/hovernet-editor-selftest.track.txt";
        Expect(ed.SaveAs(file) && !ed.dirty && ed.path == file, "saving clears the unsaved mark");
        EditorSession other;
        Expect(other.Open(file) && other.model.ToText() == ed.model.ToText() && !other.dirty && !other.CanUndo(), "the saved track opens as it was");
        Expect(!other.Open("/tmp/does-not-exist.track.txt") && other.status.find("cannot open") != std::string::npos, "a missing file is reported");
        std::remove(file.c_str());
    }

    // A malformed wall is an error, not a silent drop.
    Expect(!TrackModel::FromText(TrackText::Parse("[Room]\nId=1\nwall=1, x, 1, 52\n"), model, error) && !error.empty(), "a wall that is not four numbers is refused with a message");

    if (failures == 0) std::puts("Track source self-test passed");
    return failures == 0 ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc == 1) return SelfTest();
    const std::string command = argv[1];
    if (command == "check" && argc >= 3) {
        std::string file;
        if (!ReadFile(argv[2], file)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        const bool strict = argc >= 4 && std::string(argv[3]) == "--strict";
        const TrackText text = TrackText::Parse(file);
        if (text.Write() != file) { std::fprintf(stderr, "FAIL %s: Parse/Write changed the file\n", argv[2]); return 1; }
        TrackModel model;
        std::string error;
        if (!TrackModel::FromText(text, model, error)) { std::fprintf(stderr, "FAIL %s: %s\n", argv[2], error.c_str()); return 1; }
        const std::vector<std::string> problems = model.Problems();
        std::printf("%s: %d rooms, %d features, %zu starts, %zu elements, %zu connections, %zu other sections, %zu problems\n", argv[2], model.CountRooms(),
                    model.CountFeatures(), model.starts.size(), model.freeElements.size(), model.connections.size(), model.unknownSections.size(), problems.size());
        for (const std::string& p : problems) std::printf("  problem: %s\n", p.c_str());
        return strict && !problems.empty() ? 1 : 0;
    }
    if (command == "canonical" && argc == 4) {
        std::string file;
        if (!ReadFile(argv[2], file)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        TrackModel model;
        std::string error;
        if (!TrackModel::FromText(TrackText::Parse(file), model, error)) { std::fprintf(stderr, "FAIL %s: %s\n", argv[2], error.c_str()); return 1; }
        std::ofstream out(argv[3], std::ios::binary);
        out << model.ToText();
        return out ? 0 : 2;
    }
    if (command == "new" && argc >= 4 && std::string(argv[2]) == "ring") {
        NewTrackOptions options;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            if (a.rfind("--name=", 0) == 0) options.name = a.substr(7);
            else if (a.rfind("--rooms=", 0) == 0) options.rooms = std::atoi(a.c_str() + 8);
            else if (a.rfind("--radius=", 0) == 0) options.radius = std::atof(a.c_str() + 9);
            else if (a.rfind("--width=", 0) == 0) options.halfWidth = std::atof(a.c_str() + 8) / 2.0;
            else if (a == "--plain") options.pickups = false;
            else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        }
        TrackModel model;
        std::string error;
        if (!MakeRingTrack(options, model, error)) { std::fprintf(stderr, "cannot make the track: %s\n", error.c_str()); return 1; }
        std::ofstream out(argv[3], std::ios::binary);
        out << model.ToText();
        return out ? 0 : 2;
    }
    std::fprintf(stderr, "usage: %s [check FILE [--strict] | canonical IN OUT | new ring OUT [options]]\n", argv[0]);
    return 2;
}

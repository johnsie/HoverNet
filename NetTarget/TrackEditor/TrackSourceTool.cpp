// Command-line face of the track editor core, and its self-test (no arguments).
//   HoverNetTrackSource                         run the self-test
//   HoverNetTrackSource check FILE [--strict]   FILE must survive Parse/Write unchanged and read into the model; prints what it found
//                                               (--strict also fails when the model reports problems)
//   HoverNetTrackSource new ring OUT [--name=N] [--rooms=N] [--radius=M] [--width=M] [--plain]   make a new track from a template (no pickups with --plain)
//   HoverNetTrackSource canonical IN OUT        write the model's canonical text of IN to OUT (scripts/test-track-source-roundtrip.sh compiles both)
#include "TrackSource.h"
#include "TrackTemplates.h"

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

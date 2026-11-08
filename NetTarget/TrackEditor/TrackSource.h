// The track editor's view of a track source file (`.track.txt`, the format MazeCompiler reads; docs/track-format.md).
//
// Two layers, both free of platform code so the native editors, the browser editor and the tests share them:
//
//   TrackText   a lossless copy of the file: every line, comment, blank, case and line ending is kept, so Write() returns exactly what Parse() was
//               given. The editor never loses something it does not understand (an `[Editor]` section, a comment, an attribute a later game
//               version adds); it carries it along.
//   TrackModel  the typed content the editor edits: header, rooms, features, starts, free elements and connections, with unknown attributes and
//               sections kept in order. ToText() writes the canonical layout the compiler reads, and the compiled track is byte-identical to the one
//               compiled from the original file (checked by scripts/test-track-source-roundtrip.sh).
//
// Parity rules 1 and 2 of docs/roadmap-track-editor.md: ids are checked against TrackElementCatalogue.h (Problems()), and nothing here re-implements the
// compiler; the compiler itself still decides what compiles.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace HoverNet
{
// ---- layer 1: lossless text ------------------------------------------------------------------------------------------------------------

struct TrackTextLine
{
    std::string text;     // the line without its line ending
    std::string ending;   // "\n", "\r\n", "\r" or "" (a last line with none)
};

class TrackText
{
public:
    static TrackText Parse(const std::string& file);
    std::string Write() const;                          // exactly the text given to Parse
    const std::vector<TrackTextLine>& Lines() const { return mLines; }

    // The section name (original spelling, without brackets) when the line is a section header such as "[Room]", else an empty string.
    static std::string SectionName(const std::string& line);
    // True for a blank line or one that is only a comment (`;` or `#`, as the compiler's parser treats them).
    static bool IsBlankOrComment(const std::string& line);

private:
    std::vector<TrackTextLine> mLines;
};

// ---- layer 2: typed model ----------------------------------------------------------------------------------------------------------------

struct SurfaceRef            // `floor=0.000, 1, 51`: a height and the (factory, class) id of the surface
{
    double height = 0.0;
    int factory = 1;
    int surface = 0;
};

struct WallPoint             // `wall=7.697, 12.168, 1, 52`: a corner and the surface of the wall that starts there
{
    double x = 0.0, y = 0.0;
    int factory = 1;
    int surface = 0;
};

using Attribute = std::pair<std::string, std::string>;   // an attribute this version does not know: name and value, kept as written

struct TrackRoom             // a [Room] (isFeature false) or a [Feature] (a raised or lowered part of a room, isFeature true)
{
    bool isFeature = false;
    int id = 0;
    int parent = 0;          // features only: the room it sits in
    SurfaceRef floor;
    SurfaceRef ceiling;
    std::vector<WallPoint> walls;
    std::vector<Attribute> other;
};

struct TrackStart            // [Initial_Position]
{
    int section = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double orientation = 0.0;
    int team = 0;
    std::vector<Attribute> other;
};

struct TrackFreeElement      // [Free_Element]: a gate, pickup or speed pad
{
    int section = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double orientation = 0.0;
    int factory = 1;
    int elementType = 0;
    std::vector<Attribute> other;
};

struct TrackConnection       // a line of [Connection_List]: room, wall, other room, other wall
{
    int room1 = 0, wall1 = 0, room2 = 0, wall2 = 0;
};

struct TrackBlock            // a section the editor does not know, kept whole
{
    std::vector<TrackTextLine> lines;
};

struct TrackModel
{
    std::string description;       // as written (the `\n` sequences stay as the two characters backslash and n)
    std::string background;
    std::vector<Attribute> headerOther;
    std::vector<TrackRoom> rooms;       // rooms first, then features, as the compiler reads them
    std::vector<TrackStart> starts;
    std::vector<TrackFreeElement> freeElements;
    std::vector<TrackConnection> connections;
    std::vector<TrackBlock> unknownSections;   // written after the known ones; this is where an `[Editor]` section lives
    bool hasHeader = false;
    bool hasConnectionList = false;

    static bool FromText(const TrackText& text, TrackModel& model, std::string& error);
    std::string ToText() const;                         // canonical layout, "\n" line endings

    int CountRooms() const;
    int CountFeatures() const;

    // Things the editor can see without compiling: ids the game does not know (TrackElementCatalogue.h), a feature whose parent room is missing,
    // a start or element in a missing section, a connection to a missing room, duplicate ids. These are warnings for the Problems panel; the
    // compiler and the validator remain the authority on whether a track builds and plays.
    std::vector<std::string> Problems() const;
};
}  // namespace HoverNet

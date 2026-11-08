#include "TrackSource.h"

#include "TrackElementCatalogue.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace HoverNet
{
namespace
{
std::string Lower(std::string text)
{
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Trim(const std::string& text)
{
    std::size_t first = 0, last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

// The text of a line as the compiler's parser sees it: everything from the first `;` or `#` is a comment.
std::string StripComment(const std::string& line)
{
    const std::size_t at = line.find_first_of(";#");
    return at == std::string::npos ? line : line.substr(0, at);
}

std::vector<std::string> SplitCommas(const std::string& text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        parts.push_back(Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return parts;
}

bool ParseDouble(const std::string& text, double& value)
{
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end != nullptr && *end == '\0';
}

bool ParseInt(const std::string& text, int& value)
{
    if (text.empty()) return false;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < -2147483647L || parsed > 2147483647L) return false;
    value = static_cast<int>(parsed);
    return true;
}

// Three decimals as the bundled sources have them, or enough digits to read back as the very same double.
std::string FormatNumber(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    if (std::strtod(buffer, nullptr) == value) return buffer;
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

std::string Int(int value) { return std::to_string(value); }

// Reads the numbers of one attribute value ("1.5, 2, 1, 52") into doubles; false when any field is missing or not a number.
bool ParseNumbers(const std::string& value, std::size_t count, std::vector<double>& out)
{
    const std::vector<std::string> parts = SplitCommas(value);
    if (parts.size() != count) return false;
    out.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!ParseDouble(parts[i], out[i])) return false;
    }
    return true;
}

bool IsWhole(double value) { return value == static_cast<double>(static_cast<int>(value)); }

struct KeyValue
{
    std::string key;     // lower case, for comparing
    std::string name;    // as written
    std::string value;   // after the `=`, comment removed, not trimmed
    bool ok = false;
};

KeyValue ParseKeyValue(const std::string& line)
{
    KeyValue kv;
    const std::string text = StripComment(line);
    const std::size_t equals = text.find('=');
    if (equals == std::string::npos) return kv;
    kv.name = Trim(text.substr(0, equals));
    kv.key = Lower(kv.name);
    kv.value = text.substr(equals + 1);
    kv.ok = !kv.key.empty();
    return kv;
}

std::string TrimmedValue(const KeyValue& kv) { return Trim(kv.value); }

bool ParseSurface(const KeyValue& kv, SurfaceRef& out)
{
    std::vector<double> n;
    if (!ParseNumbers(kv.value, 3, n) || !IsWhole(n[1]) || !IsWhole(n[2])) return false;
    out.height = n[0];
    out.factory = static_cast<int>(n[1]);
    out.surface = static_cast<int>(n[2]);
    return true;
}

std::string SurfaceText(const char* name, const SurfaceRef& s)
{
    return std::string(name) + "=" + FormatNumber(s.height) + ", " + Int(s.factory) + ", " + Int(s.surface) + "\n";
}
}  // namespace

// ---- TrackText -----------------------------------------------------------------------------------------------------------------------------

TrackText TrackText::Parse(const std::string& file)
{
    TrackText text;
    std::size_t at = 0;
    while (at < file.size()) {
        TrackTextLine line;
        std::size_t end = at;
        while (end < file.size() && file[end] != '\n' && file[end] != '\r') ++end;
        line.text = file.substr(at, end - at);
        if (end < file.size()) {
            if (file[end] == '\r' && end + 1 < file.size() && file[end + 1] == '\n') { line.ending = "\r\n"; end += 2; }
            else { line.ending = std::string(1, file[end]); end += 1; }
        }
        at = end;
        text.mLines.push_back(line);
    }
    return text;
}

std::string TrackText::Write() const
{
    std::string out;
    for (const TrackTextLine& line : mLines) { out += line.text; out += line.ending; }
    return out;
}

std::string TrackText::SectionName(const std::string& line)
{
    if (line.empty() || line[0] != '[') return std::string();   // the compiler only recognises a section that starts in column 0
    const std::string text = StripComment(line);
    const std::size_t close = text.find(']');
    if (close == std::string::npos) return Trim(text.substr(1));
    return Trim(text.substr(1, close - 1));
}

bool TrackText::IsBlankOrComment(const std::string& line)
{
    return Trim(StripComment(line)).empty();
}

// ---- TrackModel ----------------------------------------------------------------------------------------------------------------------------

bool TrackModel::FromText(const TrackText& text, TrackModel& model, std::string& error)
{
    model = TrackModel();
    error.clear();
    const std::vector<TrackTextLine>& lines = text.Lines();

    std::size_t index = 0;
    // Lines before the first section (comments) belong to no section; the lossless layer keeps them, the model does not need them.
    while (index < lines.size() && TrackText::SectionName(lines[index].text).empty()) ++index;

    while (index < lines.size()) {
        const std::string name = TrackText::SectionName(lines[index].text);
        const std::string lname = Lower(name);
        std::size_t end = index + 1;
        while (end < lines.size() && TrackText::SectionName(lines[end].text).empty()) ++end;
        const std::size_t first = index + 1;   // body lines are [first, end)

        auto lineNo = [&](std::size_t i) { return std::to_string(i + 1); };

        if (lname == "header" && !model.hasHeader) {
            model.hasHeader = true;
            for (std::size_t i = first; i < end; ++i) {
                const KeyValue kv = ParseKeyValue(lines[i].text);
                if (!kv.ok) continue;
                if (kv.key == "description") model.description = kv.value;
                else if (kv.key == "background") model.background = kv.value;
                else model.headerOther.push_back(Attribute(kv.name, kv.value));
            }
        }
        else if (lname == "room" || lname == "feature") {
            TrackRoom room;
            room.isFeature = lname == "feature";
            bool haveFloor = false, haveCeiling = false;
            for (std::size_t i = first; i < end; ++i) {
                const KeyValue kv = ParseKeyValue(lines[i].text);
                if (!kv.ok) continue;
                int number = 0;
                if (kv.key == "id" && ParseInt(TrimmedValue(kv), number)) room.id = number;
                else if (kv.key == "parent" && room.isFeature && ParseInt(TrimmedValue(kv), number)) room.parent = number;
                else if (kv.key == "floor" && !haveFloor && ParseSurface(kv, room.floor)) haveFloor = true;
                else if (kv.key == "ceiling" && !haveCeiling && ParseSurface(kv, room.ceiling)) haveCeiling = true;
                else if (kv.key == "wall") {
                    std::vector<double> n;
                    if (ParseNumbers(kv.value, 4, n) && IsWhole(n[2]) && IsWhole(n[3])) {
                        WallPoint wall;
                        wall.x = n[0]; wall.y = n[1]; wall.factory = static_cast<int>(n[2]); wall.surface = static_cast<int>(n[3]);
                        room.walls.push_back(wall);
                    }
                    else { error = "line " + lineNo(i) + ": a wall needs x, y, a factory and a surface id"; return false; }
                }
                else room.other.push_back(Attribute(kv.name, kv.value));
            }
            model.rooms.push_back(room);
        }
        else if (lname == "initial_position") {
            TrackStart start;
            for (std::size_t i = first; i < end; ++i) {
                const KeyValue kv = ParseKeyValue(lines[i].text);
                if (!kv.ok) continue;
                int number = 0; double d = 0.0; std::vector<double> n;
                if (kv.key == "section" && ParseInt(TrimmedValue(kv), number)) start.section = number;
                else if (kv.key == "position" && ParseNumbers(kv.value, 3, n)) { start.x = n[0]; start.y = n[1]; start.z = n[2]; }
                else if (kv.key == "orientation" && ParseDouble(TrimmedValue(kv), d)) start.orientation = d;
                else if (kv.key == "team" && ParseInt(TrimmedValue(kv), number)) start.team = number;
                else start.other.push_back(Attribute(kv.name, kv.value));
            }
            model.starts.push_back(start);
        }
        else if (lname == "free_element") {
            TrackFreeElement element;
            for (std::size_t i = first; i < end; ++i) {
                const KeyValue kv = ParseKeyValue(lines[i].text);
                if (!kv.ok) continue;
                int number = 0; double d = 0.0; std::vector<double> n;
                if (kv.key == "section" && ParseInt(TrimmedValue(kv), number)) element.section = number;
                else if (kv.key == "position" && ParseNumbers(kv.value, 3, n)) { element.x = n[0]; element.y = n[1]; element.z = n[2]; }
                else if (kv.key == "orientation" && ParseDouble(TrimmedValue(kv), d)) element.orientation = d;
                else if (kv.key == "element_type" && ParseNumbers(kv.value, 2, n) && IsWhole(n[0]) && IsWhole(n[1])) {
                    element.factory = static_cast<int>(n[0]); element.elementType = static_cast<int>(n[1]);
                }
                else element.other.push_back(Attribute(kv.name, kv.value));
            }
            model.freeElements.push_back(element);
        }
        else if (lname == "connection_list" && !model.hasConnectionList) {
            model.hasConnectionList = true;
            for (std::size_t i = first; i < end; ++i) {
                const std::string body = Trim(StripComment(lines[i].text));
                if (body.empty()) continue;
                const std::vector<std::string> parts = SplitCommas(body);
                TrackConnection c;
                if (parts.size() != 4 || !ParseInt(parts[0], c.room1) || !ParseInt(parts[1], c.wall1) || !ParseInt(parts[2], c.room2) || !ParseInt(parts[3], c.wall2)) {
                    error = "line " + lineNo(i) + ": a connection needs four whole numbers: room, wall, other room, other wall";
                    return false;
                }
                model.connections.push_back(c);
            }
        }
        else {
            TrackBlock block;
            for (std::size_t i = index; i < end; ++i) block.lines.push_back(lines[i]);
            model.unknownSections.push_back(block);
        }
        index = end;
    }
    return true;
}

std::string TrackModel::ToText() const
{
    std::string out;
    if (hasHeader || !description.empty() || !background.empty()) {
        out += "[Header]\n";
        out += "Description=" + description + "\n";
        out += "Background=" + background + "\n";
        for (const Attribute& a : headerOther) out += a.first + "=" + a.second + "\n";
        out += "\n";
    }
    for (int pass = 0; pass < 2; ++pass) {   // rooms, then features, as the compiler reads them
        for (const TrackRoom& room : rooms) {
            if (room.isFeature != (pass == 1)) continue;
            out += room.isFeature ? "[Feature]\n" : "[Room]\n";
            out += "Id=" + Int(room.id) + "\n";
            if (room.isFeature) out += "Parent=" + Int(room.parent) + "\n";
            out += SurfaceText("floor", room.floor);
            out += SurfaceText("ceiling", room.ceiling);
            for (const WallPoint& w : room.walls)
                out += "wall=" + FormatNumber(w.x) + ", " + FormatNumber(w.y) + ", " + Int(w.factory) + ", " + Int(w.surface) + "\n";
            for (const Attribute& a : room.other) out += a.first + "=" + a.second + "\n";
            out += "\n";
        }
    }
    for (const TrackStart& s : starts) {
        out += "[Initial_Position]\n";
        out += "Section=" + Int(s.section) + "\n";
        out += "Position=" + FormatNumber(s.x) + ", " + FormatNumber(s.y) + ", " + FormatNumber(s.z) + "\n";
        out += "Orientation=" + FormatNumber(s.orientation) + "\n";
        out += "Team=" + Int(s.team) + "\n";
        for (const Attribute& a : s.other) out += a.first + "=" + a.second + "\n";
        out += "\n";
    }
    for (const TrackFreeElement& e : freeElements) {
        out += "[Free_Element]\n";
        out += "Section=" + Int(e.section) + "\n";
        out += "Position=" + FormatNumber(e.x) + ", " + FormatNumber(e.y) + ", " + FormatNumber(e.z) + "\n";
        out += "Orientation=" + FormatNumber(e.orientation) + "\n";
        out += "Element_Type=" + Int(e.factory) + ", " + Int(e.elementType) + "\n";
        for (const Attribute& a : e.other) out += a.first + "=" + a.second + "\n";
        out += "\n";
    }
    if (hasConnectionList || !connections.empty()) {
        out += "[Connection_List]\n";
        for (const TrackConnection& c : connections)
            out += Int(c.room1) + ", " + Int(c.wall1) + ", " + Int(c.room2) + ", " + Int(c.wall2) + "\n";
    }
    for (const TrackBlock& block : unknownSections) {
        out += "\n";
        for (const TrackTextLine& line : block.lines) out += line.text + "\n";
    }
    return out;
}

int TrackModel::CountRooms() const
{
    int n = 0;
    for (const TrackRoom& r : rooms) n += r.isFeature ? 0 : 1;
    return n;
}

int TrackModel::CountFeatures() const
{
    int n = 0;
    for (const TrackRoom& r : rooms) n += r.isFeature ? 1 : 0;
    return n;
}

std::vector<std::string> TrackModel::Problems() const
{
    std::vector<std::string> problems;
    std::set<int> roomIds, featureIds;
    for (const TrackRoom& r : rooms) {
        std::set<int>& ids = r.isFeature ? featureIds : roomIds;
        if (!ids.insert(r.id).second) problems.push_back(std::string(r.isFeature ? "feature " : "room ") + Int(r.id) + " is defined twice");
    }
    auto checkSurface = [&](const std::string& where, int factory, int surface) {
        if (factory != 1) { problems.push_back(where + ": factory " + Int(factory) + " is unknown (tracks use factory 1)"); return; }
        const TrackElementInfo* info = FindTrackElement(static_cast<unsigned short>(surface));
        if (info == nullptr) problems.push_back(where + ": surface " + Int(surface) + " is not an element the game knows");
        else if (info->kind != TrackElementKind::Surface) problems.push_back(where + ": " + Int(surface) + " (" + info->name + ") is not a surface");
    };
    for (const TrackRoom& r : rooms) {
        const std::string name = std::string(r.isFeature ? "feature " : "room ") + Int(r.id);
        if (r.isFeature && roomIds.count(r.parent) == 0) problems.push_back(name + ": its parent room " + Int(r.parent) + " does not exist");
        if (r.walls.size() < 3) problems.push_back(name + ": needs at least three walls");
        checkSurface(name + " floor", r.floor.factory, r.floor.surface);
        checkSurface(name + " ceiling", r.ceiling.factory, r.ceiling.surface);
        for (std::size_t i = 0; i < r.walls.size(); ++i) checkSurface(name + " wall " + Int(static_cast<int>(i) + 1), r.walls[i].factory, r.walls[i].surface);
    }
    for (std::size_t i = 0; i < starts.size(); ++i)
        if (roomIds.count(starts[i].section) == 0) problems.push_back("start " + Int(static_cast<int>(i) + 1) + ": room " + Int(starts[i].section) + " does not exist");
    for (std::size_t i = 0; i < freeElements.size(); ++i) {
        const TrackFreeElement& e = freeElements[i];
        const std::string name = "element " + Int(static_cast<int>(i) + 1);
        if (roomIds.count(e.section) == 0) problems.push_back(name + ": room " + Int(e.section) + " does not exist");
        const TrackElementInfo* info = e.factory == 1 ? FindTrackElement(static_cast<unsigned short>(e.elementType)) : nullptr;
        if (info == nullptr) problems.push_back(name + ": element " + Int(e.factory) + ", " + Int(e.elementType) + " is not one the game knows");
        else if (info->kind != TrackElementKind::PlacedElement) problems.push_back(name + ": " + Int(e.elementType) + " (" + info->name + ") cannot be placed in a track");
    }
    // guidance the validator and the lint also give: without a finish line and both checkpoints a track plays as free play; the grid has eight places
    int finishes = 0, check1 = 0, check2 = 0;
    for (const TrackFreeElement& e : freeElements) if (e.factory == 1) { finishes += e.elementType == 202; check1 += e.elementType == 203; check2 += e.elementType == 204; }
    if (!freeElements.empty() || !starts.empty() || !rooms.empty()) {
        if (finishes == 0 || check1 == 0 || check2 == 0) problems.push_back("it plays as free play until it has a finish line and both checkpoints (it has " + Int(finishes) + " finish line, " + Int(check1) + " first checkpoint and " + Int(check2) + " second checkpoint)");
        if (finishes > 1) problems.push_back(Int(finishes) + " finish lines: check that both are meant to be on the lap route");
        if (starts.size() < 8) problems.push_back("only " + Int(static_cast<int>(starts.size())) + " start(s): with more players some share a start (8 are recommended)");
    }
    for (std::size_t i = 0; i < connections.size(); ++i) {
        if (roomIds.count(connections[i].room1) == 0 || roomIds.count(connections[i].room2) == 0)
            problems.push_back("connection " + Int(static_cast<int>(i) + 1) + ": joins a room that does not exist");
    }
    return problems;
}
}  // namespace HoverNet

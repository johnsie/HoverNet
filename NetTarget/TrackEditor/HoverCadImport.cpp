// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-08.
// This file re-implements the file format and the export rules of HoverCad (HoverCad/HoverCadDoc.cpp, HoverCad/HoverTypes.cpp), which are
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc., licensed under the GrokkSoft HoverRace SourceCode License v1.0. See LICENSE and
// docs/licence-compliance.md. See repository history for authorship and detailed changes.
#include "HoverCadImport.h"

#include "TrackElementCatalogue.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

namespace HoverNet
{
namespace
{
// HoverCad's tables (HoverCad/HoverTypes.cpp): the number it stored for a texture or object, and the game's factory and class id for it.
struct HcType { int serial, dll, type; };
const HcType kWall[] = {{0, 1, 50}, {19, 1, 72}, {20, 1, 73}, {1, 1, 52}, {2, 1, 53}, {3, 1, 54}, {4, 1, 55}, {5, 1, 56}, {6, 1, 57}, {21, 1, 70}, {22, 1, 71}, {7, 1, 58}, {8, 1, 59},
                        {9, 1, 60}, {10, 1, 61}, {11, 1, 62}, {12, 1, 63}, {13, 1, 64}, {14, 1, 65}, {15, 1, 66}, {16, 1, 67}, {17, 1, 68}, {18, 1, 69}};
const HcType kFloor[] = {{0, 1, 51}, {1, 1, 58}, {2, 1, 63}, {3, 1, 64}, {4, 1, 65}, {5, 1, 66}, {6, 1, 67}};
const HcType kCeiling[] = {{0, 1, 50}, {1, 1, 51}, {2, 1, 58}, {3, 1, 63}, {4, 1, 64}, {5, 1, 65}, {6, 1, 66}, {7, 1, 67}};
const HcType kObject[] = {{0, 0, 1}, {1, 0, 2}, {2, 0, 3}, {3, 0, 4}, {4, 0, 5}, {5, 0, 6}, {6, 0, 7}, {7, 0, 8}, {8, 0, 9}, {9, 0, 10}, {12, 1, 202}, {16, 1, 203}, {17, 1, 204},
                          {10, 1, 200}, {11, 1, 201}, {13, 1, 170}, {14, 1, 151}, {15, 1, 152}};
template <std::size_t N> int IndexOf(int serial, const HcType (&list)[N])   // HoverCad's GetTypeIndex: the last match, else the first entry
{
    int found = 0;
    for (std::size_t i = 0; i < N; ++i) if (list[i].serial == serial) found = static_cast<int>(i);
    return found;
}
const int kStartCount = 10;   // the first ten objects (dll 0) are the starting positions

struct Node { int x = 0, y = 0; int item = 0; std::vector<int> anchors; };
struct Anchor { int texture = 0, node = 0, polygon = 0; int exportId = 0; };
struct Polygon { bool feature = false; int floorSerial = 0, ceilingSerial = 0, floorLevel = 0, ceilingLevel = 0; std::vector<int> anchors; int exportId = 0; };
struct Item { int node = 0, element = 0, distance = 0, orientation = 0; };

// the MFC archive's object map: class descriptions and objects share one numbering that starts at 1
struct Entry { enum Kind { None, Class, NodeObj, AnchorObj, PolygonObj, ItemObj } kind = None; std::string className; int slot = -1; };

struct Archive
{
    const std::vector<std::uint8_t>& d;
    std::size_t p = 0;
    bool ok = true;
    std::string error;
    std::vector<Entry> map = std::vector<Entry>(1);
    std::vector<Node> nodes; std::vector<Anchor> anchors; std::vector<Polygon> polygons; std::vector<Item> items;
    int depth = 0;

    explicit Archive(const std::vector<std::uint8_t>& bytes) : d(bytes) {}

    bool Fail(const std::string& why) { if (ok) { ok = false; error = why; } return false; }
    std::uint32_t U32() { if (p + 4 > d.size()) { Fail("the file ends too soon"); return 0; } const std::uint32_t v = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (static_cast<std::uint32_t>(d[p + 3]) << 24); p += 4; return v; }
    int Int() { return static_cast<int>(U32()); }
    std::uint32_t U16() { if (p + 2 > d.size()) { Fail("the file ends too soon"); return 0; } const std::uint32_t v = d[p] | (d[p + 1] << 8); p += 2; return v; }
    std::string Bytes(std::size_t n) { if (p + n > d.size()) { Fail("the file ends too soon"); return std::string(); } std::string s(reinterpret_cast<const char*>(&d[p]), n); p += n; return s; }
    std::string CString()   // MFC's length-prefixed string: a byte, or 0xFF then a word, or 0xFF 0xFFFF then a long
    {
        std::uint32_t n = p < d.size() ? d[p++] : 0;
        if (n == 0xFF) { n = U16(); if (n == 0xFFFF) n = U32(); }
        if (n > 1000000) { Fail("a text in the file is too long"); return std::string(); }
        return Bytes(n);
    }

    // one object pointer: returns the object's map index, 0 for none. New objects are read in place, as MFC wrote them.
    int Pointer(Entry::Kind expect)
    {
        if (!ok) return 0;
        const std::uint32_t tag = U16();
        if (!ok || tag == 0) return 0;
        int classIndex = 0;
        if (tag == 0xFFFF) {   // a class seen for the first time: its version, its name
            U16();
            const std::uint32_t len = U16();
            if (len == 0 || len > 64) { Fail("a class name in the file is not valid"); return 0; }
            Entry cls; cls.kind = Entry::Class; cls.className = Bytes(len);
            map.push_back(cls);
            classIndex = static_cast<int>(map.size()) - 1;
        }
        else {
            std::uint32_t obTag = tag == 0x7FFF ? U32() : (((tag & 0x8000u) << 16) | (tag & 0x7FFFu));
            if (obTag & 0x80000000u) classIndex = static_cast<int>(obTag & 0x7FFFFFFFu);   // a class seen before: a new object of it follows
            else {                                                                             // an object seen before
                if (obTag >= map.size() || map[obTag].kind == Entry::Class || map[obTag].kind == Entry::None) { Fail("the file refers to an object that is not there"); return 0; }
                if (map[obTag].kind != expect) { Fail("the file refers to the wrong kind of object"); return 0; }
                return static_cast<int>(obTag);
            }
        }
        if (classIndex <= 0 || classIndex >= static_cast<int>(map.size()) || map[classIndex].kind != Entry::Class) { Fail("the file refers to a class that is not there"); return 0; }
        const std::string& name = map[static_cast<std::size_t>(classIndex)].className;
        Entry e;
        if (name == "HCNode") e.kind = Entry::NodeObj;
        else if (name == "HCAnchorWall") e.kind = Entry::AnchorObj;
        else if (name == "HCPolygon") e.kind = Entry::PolygonObj;
        else if (name == "HCItem") e.kind = Entry::ItemObj;
        else { Fail("the file holds a " + name + ", which HoverCad projects do not"); return 0; }
        if (e.kind != expect) { Fail("the file holds a " + name + " where another kind of object belongs"); return 0; }
        if (++depth > 2000) { Fail("the file nests too deeply"); return 0; }
        const int index = static_cast<int>(map.size());
        switch (e.kind) {
        case Entry::NodeObj: e.slot = static_cast<int>(nodes.size()); nodes.emplace_back(); break;
        case Entry::AnchorObj: e.slot = static_cast<int>(anchors.size()); anchors.emplace_back(); break;
        case Entry::PolygonObj: e.slot = static_cast<int>(polygons.size()); polygons.emplace_back(); break;
        default: e.slot = static_cast<int>(items.size()); items.emplace_back(); break;
        }
        map.push_back(e);
        Read(index);
        --depth;
        return index;
    }

    // the list HoverCad's CSerialObjList writes: a count, then the pointers
    std::vector<int> List(Entry::Kind kind)
    {
        std::vector<int> out;
        const int count = Int();
        if (!ok) return out;
        if (count < 0 || count > 200000) { Fail("a list in the file has an impossible length"); return out; }
        for (int i = 0; i < count && ok; ++i) out.push_back(Pointer(kind));
        return out;
    }

    void Read(int index)
    {
        const Entry e = map[static_cast<std::size_t>(index)];
        if (e.kind == Entry::NodeObj) {
            const int x = Int(), y = Int();
            const int item = Pointer(Entry::ItemObj);
            std::vector<int> list = List(Entry::AnchorObj);
            Node& n = nodes[static_cast<std::size_t>(e.slot)];
            n.x = x; n.y = y; n.item = item; n.anchors = list;
        }
        else if (e.kind == Entry::AnchorObj) {
            const int texture = Int();
            const int node = Pointer(Entry::NodeObj);
            const int polygon = Pointer(Entry::PolygonObj);
            Anchor& a = anchors[static_cast<std::size_t>(e.slot)];
            a.texture = texture; a.node = node; a.polygon = polygon;
        }
        else if (e.kind == Entry::PolygonObj) {
            const int feature = Int(), floorSerial = Int(), ceilingSerial = Int(), floorLevel = Int(), ceilingLevel = Int();
            std::vector<int> list = List(Entry::AnchorObj);
            Polygon& g = polygons[static_cast<std::size_t>(e.slot)];
            g.feature = feature != 0; g.floorSerial = floorSerial; g.ceilingSerial = ceilingSerial; g.floorLevel = floorLevel; g.ceilingLevel = ceilingLevel; g.anchors = list;
        }
        else if (e.kind == Entry::ItemObj) {
            const int node = Pointer(Entry::NodeObj);
            const int element = Int(), distance = Int(), orientation = Int();
            Item& it = items[static_cast<std::size_t>(e.slot)];
            it.node = node; it.element = element; it.distance = distance; it.orientation = orientation;
        }
    }
};

// HoverCad's HCPolygon::IsIncluding: a bounding-box test, then every edge must have the point on its left or on it (so the rooms are convex, as HoverCad made them)
bool Includes(const Archive& a, const Polygon& g, int px, int py)
{
    bool xMin = false, xMax = false, yMin = false, yMax = false;
    for (int ai : g.anchors) {
        const Node& n = a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(a.anchors[static_cast<std::size_t>(a.map[static_cast<std::size_t>(ai)].slot)].node)].slot)];
        if (px >= n.x) xMin = true;
        if (px <= n.x) xMax = true;
        if (py >= n.y) yMin = true;
        if (py <= n.y) yMax = true;
    }
    if (!(xMin && xMax && yMin && yMax)) return false;
    for (std::size_t i = 0; i < g.anchors.size(); ++i) {
        const Anchor& cur = a.anchors[static_cast<std::size_t>(a.map[static_cast<std::size_t>(g.anchors[i])].slot)];
        const Anchor& nxt = a.anchors[static_cast<std::size_t>(a.map[static_cast<std::size_t>(g.anchors[(i + 1) % g.anchors.size()])].slot)];
        const Node& n = a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(cur.node)].slot)];
        const Node& m = a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(nxt.node)].slot)];
        const long long cross = static_cast<long long>(m.y - n.y) * (px - n.x) - static_cast<long long>(m.x - n.x) * (py - n.y);
        if (cross < 0) return false;
    }
    return true;
}

std::string Basename(const std::string& path)
{
    const std::size_t at = path.find_last_of("/\\\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}
}  // namespace

bool LooksLikeHoverCad(const std::vector<std::uint8_t>& b)
{
    // a count (4 bytes), the new-class tag 0xFFFF, a version, the name length 6 and "HCNode": the first thing HoverCad writes
    if (b.size() < 16) return false;
    return b[4] == 0xFF && b[5] == 0xFF && b[8] == 6 && b[9] == 0 && std::memcmp(&b[10], "HCNode", 6) == 0;
}

bool ImportHoverCad(const std::vector<std::uint8_t>& bytes, TrackModel& model, std::string& error, std::vector<std::string>& notes)
{
    model = TrackModel();
    error.clear();
    notes.clear();
    if (!LooksLikeHoverCad(bytes)) { error = "this is not a HoverCad project"; return false; }
    Archive a(bytes);
    const std::vector<int> nodeList = a.List(Entry::NodeObj);
    const std::vector<int> polygonList = a.List(Entry::PolygonObj);
    const std::string background = a.ok ? a.CString() : std::string();
    const std::string description = a.ok ? a.CString() : std::string();
    if (!a.ok) { error = a.error; return false; }

    auto nodeOf = [&](int anchorMapIndex) -> const Node& { return a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(a.anchors[static_cast<std::size_t>(a.map[static_cast<std::size_t>(anchorMapIndex)].slot)].node)].slot)]; };
    auto anchorOf = [&](int mapIndex) -> Anchor& { return a.anchors[static_cast<std::size_t>(a.map[static_cast<std::size_t>(mapIndex)].slot)]; };
    auto polyOf = [&](int mapIndex) -> Polygon& { return a.polygons[static_cast<std::size_t>(a.map[static_cast<std::size_t>(mapIndex)].slot)]; };
    for (int pi : polygonList) {
        if (a.map[static_cast<std::size_t>(pi)].kind != Entry::PolygonObj) { error = "the file's polygon list is not valid"; return false; }
        const Polygon& g = polyOf(pi);
        if (g.anchors.size() < 3) { error = "a polygon in the file has fewer than three walls"; return false; }
        for (int ai : g.anchors) if (a.map[static_cast<std::size_t>(ai)].kind != Entry::AnchorObj || anchorOf(ai).node == 0) { error = "a wall in the file has no corner"; return false; }
    }

    // the header: the description with its line ends as the compiler's text writes them; the background re-pointed at the game's own folder
    for (std::size_t i = 0; i < description.size(); ++i) {
        if (description[i] == '\r') { model.description += "\\n"; ++i; }
        else model.description += description[i];
    }
    const std::string picture = Basename(background);
    model.background = "NetTarget/mazes/" + (picture.empty() ? std::string("BG-CITY.pcx") : picture);
    if (background != model.background) notes.push_back("The background picture was \"" + background + "\"; it is now " + model.background + " (the game's own folder).");
    model.hasHeader = true;

    // polygons: each is numbered by its place in the list (rooms and features share the numbering, as HoverCad's export did)
    int exportId = 1;
    for (int pi : polygonList) polyOf(pi).exportId = exportId++;
    auto roomForNode = [&](const Node& n) -> int {   // the first room that includes the point (HoverCad's GetRoomForNode); 0 when none
        for (int pi : polygonList) if (!polyOf(pi).feature && Includes(a, polyOf(pi), n.x, n.y)) return pi;
        return 0;
    };
    auto roomFloor = [&](int pi) { return polyOf(pi).floorLevel; };
    for (int pi : polygonList) {
        const Polygon& g = polyOf(pi);
        TrackRoom room;
        room.isFeature = g.feature;
        room.id = g.exportId;
        if (g.feature) {
            const int parent = roomForNode(nodeOf(g.anchors.front()));
            if (parent == 0) { notes.push_back("A feature (number " + std::to_string(g.exportId) + ") is not inside any room, so it was left out, as HoverCad's own export left it out."); continue; }
            room.parent = polyOf(parent).exportId;
        }
        const HcType& fl = kFloor[IndexOf(g.floorSerial, kFloor)];
        const HcType& ce = kCeiling[IndexOf(g.ceilingSerial, kCeiling)];
        room.floor.height = g.floorLevel / 1000.0; room.floor.factory = fl.dll; room.floor.surface = fl.type;
        room.ceiling.height = g.ceilingLevel / 1000.0; room.ceiling.factory = ce.dll; room.ceiling.surface = ce.type;
        for (int ai : g.anchors) {
            const Node& n = nodeOf(ai);
            const HcType& wt = kWall[IndexOf(anchorOf(ai).texture, kWall)];
            WallPoint w; w.x = n.x / 1000.0; w.y = n.y / 1000.0; w.factory = wt.dll; w.surface = wt.type;
            room.walls.push_back(w);
        }
        model.rooms.push_back(room);
    }

    // items: starts first (the first ten objects), then everything else, in node order
    auto itemOfNode = [&](int nodeMapIndex) -> const Item* { const Node& n = a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(nodeMapIndex)].slot)]; return n.item ? &a.items[static_cast<std::size_t>(a.map[static_cast<std::size_t>(n.item)].slot)] : nullptr; };
    auto nodeAt = [&](int mapIndex) -> const Node& { return a.nodes[static_cast<std::size_t>(a.map[static_cast<std::size_t>(mapIndex)].slot)]; };
    for (int counter = 0; counter < kStartCount; ++counter) {
        const Item* found = nullptr;
        for (int ni : nodeList) { const Item* it = itemOfNode(ni); if (it && IndexOf(it->element, kObject) == counter && kObject[IndexOf(it->element, kObject)].dll == 0) { found = it; break; } }
        if (!found) continue;   // HoverCad asked about a missing start; a track needs at least one, and the problems list will say how many there are
        const int room = found->node ? roomForNode(nodeAt(found->node)) : 0;
        if (room == 0) { notes.push_back("Start " + std::to_string(counter + 1) + " is not on the track, so it was left out."); continue; }
        TrackStart s;
        s.section = polyOf(room).exportId;
        s.x = nodeAt(found->node).x / 1000.0; s.y = nodeAt(found->node).y / 1000.0; s.z = (found->distance + roomFloor(room)) / 1000.0;
        s.orientation = found->orientation / 1000.0;
        s.team = counter + 1;
        model.starts.push_back(s);
    }
    for (int ni : nodeList) {
        const Item* it = itemOfNode(ni);
        if (!it || !it->node) continue;
        const HcType& t = kObject[IndexOf(it->element, kObject)];
        if (t.dll <= 0) continue;
        const int room = roomForNode(nodeAt(it->node));
        if (room == 0) { notes.push_back("An element (" + std::to_string(t.dll) + ", " + std::to_string(t.type) + ") is not on the track, so it was left out."); continue; }
        TrackFreeElement e;
        e.section = polyOf(room).exportId;
        e.x = nodeAt(it->node).x / 1000.0; e.y = nodeAt(it->node).y / 1000.0; e.z = (it->distance + roomFloor(room)) / 1000.0;
        e.orientation = it->orientation / 1000.0;
        e.factory = t.dll; e.elementType = t.type;
        model.freeElements.push_back(e);
    }

    // connections: two rooms share a wall when, at the node where one wall starts, another room's wall ends at the node where the first one ends
    for (int pi : polygonList) { int id = 0; for (int ai : polyOf(pi).anchors) anchorOf(ai).exportId = id++; }
    auto prevOf = [&](int polygonMapIndex, int anchorMapIndex) -> int {
        const std::vector<int>& list = polyOf(polygonMapIndex).anchors;
        for (std::size_t i = 0; i < list.size(); ++i) if (list[i] == anchorMapIndex) return list[(i + list.size() - 1) % list.size()];
        return anchorMapIndex;
    };
    auto nextOf = [&](int polygonMapIndex, int anchorMapIndex) -> int {
        const std::vector<int>& list = polyOf(polygonMapIndex).anchors;
        for (std::size_t i = 0; i < list.size(); ++i) if (list[i] == anchorMapIndex) return list[(i + 1) % list.size()];
        return anchorMapIndex;
    };
    for (int pi : polygonList) {
        if (polyOf(pi).feature) continue;
        for (int ai : polyOf(pi).anchors) {
            if (anchorOf(ai).exportId == -1) continue;
            const Node& here = nodeAt(anchorOf(ai).node);
            for (int bi : here.anchors) {
                if (bi == ai) continue;
                Anchor& other = anchorOf(bi);
                if (a.map[static_cast<std::size_t>(other.polygon)].kind != Entry::PolygonObj || polyOf(other.polygon).feature) continue;
                const int otherPrev = prevOf(other.polygon, bi);
                if (anchorOf(otherPrev).exportId == -1) continue;
                if (anchorOf(nextOf(pi, ai)).node == anchorOf(otherPrev).node) {
                    TrackConnection c;
                    c.room1 = polyOf(pi).exportId; c.wall1 = anchorOf(ai).exportId;
                    c.room2 = polyOf(other.polygon).exportId; c.wall2 = anchorOf(otherPrev).exportId;
                    model.connections.push_back(c);
                    anchorOf(ai).exportId = -1;
                    anchorOf(otherPrev).exportId = -1;
                }
            }
        }
    }
    model.hasConnectionList = true;
    return true;
}
}  // namespace HoverNet

#include "CraftDesign.h"

#include "CraftNames.h"

#include <algorithm>

namespace Authority
{
namespace
{
constexpr char kMagic[4] = {'H', 'C', 'D', '1'};

std::uint32_t Fnv32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) { h ^= data[i]; h *= 16777619u; }
    return h;
}
bool NameOk(const std::string& name)
{
    if (name.size() > static_cast<std::size_t>(kMaxDesignNameBytes)) return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return c >= 32 && c <= 126; });
}
bool Fail(std::string* error, const char* why) { if (error != nullptr) *error = why; return false; }
}

CraftDesign::CraftDesign()
{
    for (auto& entry : palette) entry = {{0, 0, 0}};
    // A starting set of livery colours the editor can show before the player changes any.
    static const std::uint8_t start[kPaintColours][3] = {{0, 0, 0}, {255, 255, 255}, {20, 20, 20}, {220, 30, 30}, {240, 120, 20}, {245, 215, 30}, {40, 180, 70}, {30, 190, 190},
                                                         {40, 90, 220}, {120, 60, 200}, {230, 70, 170}, {150, 150, 150}, {90, 55, 30}, {250, 200, 160}, {10, 40, 90}, {255, 240, 200}};
    for (int i = 0; i < kPaintColours; ++i) palette[i] = {{start[i][0], start[i][1], start[i][2]}};
}

bool CraftDesign::operator==(const CraftDesign& o) const
{
    return craft == o.craft && length == o.length && width == o.width && height == o.height && taper == o.taper && body == o.body && bodyStrength == o.bodyStrength &&
           accent == o.accent && accentStrength == o.accentStrength && palette == o.palette && paint == o.paint && name == o.name;
}

bool CraftDesign::IsPlain() const
{
    if (length != 0 || width != 0 || height != 0 || taper != 0 || bodyStrength != 0 || accentStrength != 0) return false;
    return std::all_of(paint.begin(), paint.end(), [](std::uint8_t p) { return p == 0; });
}

std::vector<std::uint8_t> EncodeCraftDesign(const CraftDesign& d)
{
    std::vector<std::uint8_t> out(kMagic, kMagic + 4);
    out.push_back(d.craft);
    for (std::int8_t s : {d.length, d.width, d.height, d.taper}) out.push_back(static_cast<std::uint8_t>(s));
    out.insert(out.end(), d.body.begin(), d.body.end());
    out.push_back(d.bodyStrength);
    out.insert(out.end(), d.accent.begin(), d.accent.end());
    out.push_back(d.accentStrength);
    for (const auto& entry : d.palette) out.insert(out.end(), entry.begin(), entry.end());
    for (std::size_t i = 0; i < d.paint.size(); i += 2) out.push_back(static_cast<std::uint8_t>((d.paint[i] & 15) | ((d.paint[i + 1] & 15) << 4)));
    out.push_back(static_cast<std::uint8_t>(d.name.size()));
    out.insert(out.end(), d.name.begin(), d.name.end());
    const std::uint32_t sum = Fnv32(out.data(), out.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(sum >> (8 * i)));
    return out;
}

bool DecodeCraftDesign(const std::uint8_t* data, std::size_t size, CraftDesign& design, std::string* error)
{
    const std::size_t fixed = 4 + 1 + 4 + 3 + 1 + 3 + 1 + kPaintColours * 3 + kPaintSize * kPaintSize / 2 + 1;
    if (data == nullptr || size < fixed + 4 || size > kMaxDesignBytes) return Fail(error, "wrong size");
    if (!std::equal(kMagic, kMagic + 4, data)) return Fail(error, "not a craft design");
    std::uint32_t sum = 0;
    for (int i = 0; i < 4; ++i) sum |= static_cast<std::uint32_t>(data[size - 4 + i]) << (8 * i);
    if (sum != Fnv32(data, size - 4)) return Fail(error, "damaged (checksum)");

    CraftDesign d;
    std::size_t at = 4;
    d.craft = data[at++];
    if (d.craft >= kCraftCount) return Fail(error, "unknown hull");
    const auto shape = [&](std::int8_t& value, int limit) {
        value = static_cast<std::int8_t>(data[at++]);
        return value >= -limit && value <= limit;
    };
    if (!shape(d.length, kMaxShapePercent) || !shape(d.width, kMaxShapePercent) || !shape(d.height, kMaxShapePercent) || !shape(d.taper, kMaxTaperPercent)) return Fail(error, "shape out of range");
    for (auto& c : d.body) c = data[at++];
    d.bodyStrength = data[at++];
    for (auto& c : d.accent) c = data[at++];
    d.accentStrength = data[at++];
    for (auto& entry : d.palette) for (auto& c : entry) c = data[at++];
    for (std::size_t i = 0; i < d.paint.size(); i += 2) { d.paint[i] = data[at] & 15; d.paint[i + 1] = data[at] >> 4; ++at; }
    const std::size_t nameLength = data[at++];
    if (nameLength > static_cast<std::size_t>(kMaxDesignNameBytes) || at + nameLength + 4 != size) return Fail(error, "bad name length");
    d.name.assign(reinterpret_cast<const char*>(data + at), nameLength);
    if (!NameOk(d.name)) return Fail(error, "name has characters that are not allowed");
    design = d;
    return true;
}

CraftDesign SanitiseCraftDesign(const CraftDesign& in)
{
    CraftDesign d = in;
    if (d.craft >= kCraftCount) d.craft = 0;
    const auto clamp = [](std::int8_t v, int limit) { return static_cast<std::int8_t>(std::max(-limit, std::min(limit, static_cast<int>(v)))); };
    d.length = clamp(d.length, kMaxShapePercent); d.width = clamp(d.width, kMaxShapePercent); d.height = clamp(d.height, kMaxShapePercent); d.taper = clamp(d.taper, kMaxTaperPercent);
    for (auto& p : d.paint) p &= 15;
    std::string clean;
    for (char c : d.name) if (c >= 32 && c <= 126 && clean.size() < static_cast<std::size_t>(kMaxDesignNameBytes)) clean.push_back(c);
    d.name = clean;
    return d;
}

std::uint64_t CraftDesignHash(const CraftDesign& d)
{
    const std::vector<std::uint8_t> bytes = EncodeCraftDesign(d);
    std::uint64_t h = 1469598103934665603ULL;
    for (std::uint8_t b : bytes) { h ^= b; h *= 1099511628211ULL; }
    return h;
}
}

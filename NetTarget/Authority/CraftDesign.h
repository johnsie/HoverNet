// CraftDesign.h -- a player's own look for their hovercraft: the shape, the colours and a painted livery. COSMETIC ONLY.
//
// Nothing in a design reaches the simulation. The craft number (`craft`) selects the Garage preview hull; the shape sliders, colours and paint change only
// what is drawn, so a design can never make a craft faster, lighter or harder to hit, and the race hash, replays and golden files are untouched
// (docs/craft-designer.md). A design is small (at most kMaxDesignBytes) so it can travel through the RaceServer to every player in a race (no
// client-to-client traffic) and be kept in the browser or submitted to the optional account service.
#ifndef AUTHORITY_CRAFT_DESIGN_H
#define AUTHORITY_CRAFT_DESIGN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Authority
{
constexpr int kPaintSize = 32;                  // the livery is kPaintSize x kPaintSize pixels
constexpr int kPaintColours = 16;               // palette entries; entry 0 is "no paint" (transparent)
constexpr int kMaxShapePercent = 25;            // length, width and height: -25..+25 %
constexpr int kMaxTaperPercent = 30;            // front narrower (-) or wider (+) than the back
constexpr int kMaxDesignNameBytes = 24;
constexpr std::size_t kMaxDesignBytes = 640;

struct CraftDesign
{
    std::uint8_t craft = 0;                                       // the hull driven: its performance, unchanged by anything below
    std::int8_t length = 0, width = 0, height = 0, taper = 0;     // shape, in percent
    std::array<std::uint8_t, 3> body{{255, 255, 255}};            // the colour the hull is tinted towards
    std::uint8_t bodyStrength = 0;                                // 0 = the hull's own colours .. 255 = fully the body colour
    std::array<std::uint8_t, 3> accent{{255, 255, 255}};          // the cockpit and trim
    std::uint8_t accentStrength = 0;
    std::array<std::array<std::uint8_t, 3>, kPaintColours> palette{};   // the livery's colours (entry 0 unused)
    std::array<std::uint8_t, kPaintSize * kPaintSize> paint{};          // palette index per pixel, row by row; 0 = unpainted
    std::string name;                                             // printable ASCII, at most kMaxDesignNameBytes

    CraftDesign();
    bool operator==(const CraftDesign& other) const;
    bool IsPlain() const;                                         // no shape, colour or paint: the stock hull
};

std::vector<std::uint8_t> EncodeCraftDesign(const CraftDesign& design);
// False (and a reason) for anything malformed or out of range; nothing is clamped silently, so a design that decodes is exactly what the sender made.
bool DecodeCraftDesign(const std::uint8_t* data, std::size_t size, CraftDesign& design, std::string* error = nullptr);
// A forced-valid copy: ranges clamped, the name cleaned. For editing code; decoding rejects instead.
CraftDesign SanitiseCraftDesign(const CraftDesign& design);
std::uint64_t CraftDesignHash(const CraftDesign& design);          // identifies a design in caches
}

#endif

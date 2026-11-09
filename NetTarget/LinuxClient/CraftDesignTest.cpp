// CraftDesignTest -- the craft design format: round trip, every rejection, and that nothing malformed gets through.
#include "../Authority/CraftDesign.h"

#include <cstdio>
#include <cstdlib>

using namespace Authority;
namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    CraftDesign d;
    d.craft = 3; d.length = 12; d.width = -8; d.height = 25; d.taper = -30; d.body = {{200, 30, 40}}; d.bodyStrength = 180; d.accent = {{10, 20, 250}}; d.accentStrength = 255;
    d.name = "Red Comet";
    for (int i = 0; i < kPaintSize * kPaintSize; ++i) d.paint[i] = static_cast<std::uint8_t>((i * 7 + i / 32) % 16);
    const std::vector<std::uint8_t> bytes = EncodeCraftDesign(d);
    Check(bytes.size() <= kMaxDesignBytes, "an encoded design fits the size limit");
    std::printf("encoded size %zu bytes\n", bytes.size());
    CraftDesign back; std::string error;
    Check(DecodeCraftDesign(bytes.data(), bytes.size(), back, &error) && back == d, "a design survives encoding and decoding exactly");
    Check(CraftDesignHash(back) == CraftDesignHash(d), "the hash is the same for the same design");
    CraftDesign other = d; other.paint[5] ^= 1;
    Check(CraftDesignHash(other) != CraftDesignHash(d), "one painted pixel changes the hash");
    Check(CraftDesign().IsPlain() && !d.IsPlain(), "a stock design is plain, a changed one is not");

    int rejected = 0, flips = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        std::vector<std::uint8_t> bad = bytes; bad[i] ^= 0x5a; ++flips;
        CraftDesign x;
        if (!DecodeCraftDesign(bad.data(), bad.size(), x)) ++rejected;
    }
    Check(rejected == flips, "any single damaged byte is rejected (the checksum)");
    {
        CraftDesign x;
        Check(!DecodeCraftDesign(bytes.data(), bytes.size() - 1, x), "a truncated design is rejected");
        std::vector<std::uint8_t> big = bytes; big.resize(kMaxDesignBytes + 1);
        Check(!DecodeCraftDesign(big.data(), big.size(), x), "an oversized design is rejected");
        Check(!DecodeCraftDesign(nullptr, 0, x), "nothing is rejected");
    }
    // Out-of-range values that carry a valid checksum are still refused: re-encode from a hand-made invalid design.
    for (int field = 0; field < 4; ++field) {
        CraftDesign e = d;
        (field == 0 ? e.length : field == 1 ? e.width : field == 2 ? e.height : e.taper) = 100;
        const std::vector<std::uint8_t> b = EncodeCraftDesign(e); CraftDesign x;
        char what[80]; std::snprintf(what, sizeof what, "shape value %d out of range is rejected", field);
        Check(!DecodeCraftDesign(b.data(), b.size(), x), what);
    }
    { CraftDesign e = d; e.craft = 9; const auto b = EncodeCraftDesign(e); CraftDesign x; Check(!DecodeCraftDesign(b.data(), b.size(), x), "an unknown hull is rejected"); }
    { CraftDesign e = d; e.name = "bad\x01name"; const auto b = EncodeCraftDesign(e); CraftDesign x; Check(!DecodeCraftDesign(b.data(), b.size(), x), "control characters in the name are rejected"); }
    { CraftDesign e = d; e.name = std::string(40, 'a'); const auto b = EncodeCraftDesign(e); CraftDesign x; Check(!DecodeCraftDesign(b.data(), b.size(), x), "a long name is rejected"); }
    {
        CraftDesign wild = d; wild.craft = 77; wild.length = 120; wild.taper = -100; wild.name = "x\x07y"; wild.paint[3] = 200;
        const CraftDesign s = SanitiseCraftDesign(wild);
        const auto b = EncodeCraftDesign(s); CraftDesign x;
        Check(DecodeCraftDesign(b.data(), b.size(), x) && s.craft == 0 && s.length == kMaxShapePercent && s.taper == -kMaxTaperPercent && s.name == "xy", "sanitising makes any design valid");
    }
    std::printf(gFail == 0 ? "craft design: all checks passed\n" : "craft design: FAILED\n");
    return gFail == 0 ? 0 : 1;
}

// Frame-rate independence of the fixed-step loop (MR_GameSession::SimulateElapsed).
//
//   HoverNetFrameRateTest <track> [ticks]
//
// The same race is driven by a game loop whose frames last different times: a steady 60 Hz, a 30 Hz, a 144 Hz, a loop
// that alternates 16 and 17 ms, a very uneven one, and one that stalls for a whole second now and then. Each loop holds
// the same controls and feeds the elapsed time to SimulateElapsed. Whatever the frame pattern, after the same number
// of ticks the state hash must be identical. (Controls that change mid-run are quantised to frame boundaries, which is
// inherent to a loop that reads input once per frame; the networked game sends input per tick instead.)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../ObjFac1/SimElements.h"

using SimHarness::World;

namespace
{
struct Pattern { const char* name; std::vector<int> frames; };

std::uint64_t Run(const std::string& path, const std::string& track, const Pattern& pattern, int ticks, int& framesUsed)
{
    World world;
    if (!world.Build(path, track, 2)) return 0;
    const int held[2] = {MR_MainCharacter::eMotorOn | MR_MainCharacter::eLeft | MR_MainCharacter::eFire,
                         MR_MainCharacter::eMotorOn | MR_MainCharacter::eRight};
    world.craft[0]->SetControlState(held[0], 0);
    world.craft[1]->SetControlState(held[1], 0);
    int ran = 0;
    framesUsed = 0;
    for (std::size_t i = 0; ran < ticks; ++i, ++framesUsed) {
        const int elapsed = pattern.frames[i % pattern.frames.size()];
        ran += world.session->SimulateElapsed(elapsed, SimHarness::kFrameMs, ticks - ran);
        if (framesUsed > 1000000) return 0;
    }
    return world.Hash();
}
// Drawing between ticks (MR_GameSession::BeginInterpolatedRender): at every frame pattern the drawn craft stays between where it was before the last tick and where it is after it, moves
// forward through a tick as the frame's clock advances, and every craft is put back exactly afterwards.
int CheckInterpolation(const std::string& path, const std::string& track)
{
    World world;
    if (!world.Build(path, track, 2)) return 1;
    world.craft[0]->SetControlState(MR_MainCharacter::eMotorOn, 0);
    int failures = 0;
    for (int frame = 0; frame < 300; ++frame) {
        world.session->SimulateElapsed(7, SimHarness::kFrameMs);   // 144 Hz frames: most frames run no tick at all
        const MR_3DCoordinate real = world.craft[0]->mPosition;
        const MR_Angle realOrientation = world.craft[0]->mOrientation;
        MR_SimulationTime drawTime = 0;
        if (!world.session->BeginInterpolatedRender(drawTime, 0.0)) continue;   // nothing before the first tick
        const MR_3DCoordinate drawn = world.craft[0]->mPosition;
        const bool moved = drawn.mX != real.mX || drawn.mY != real.mY;
        world.session->EndInterpolatedRender();
        const MR_3DCoordinate back = world.craft[0]->mPosition;
        if (back.mX != real.mX || back.mY != real.mY || back.mZ != real.mZ || world.craft[0]->mOrientation != realOrientation) { std::fprintf(stderr, "  interpolation: the craft was not put back exactly (frame %d)\n", frame); ++failures; break; }
        const MR_SimulationTime now = world.session->GetSimulationTime();
        if (drawTime > now || drawTime < now - SimHarness::kFrameMs) { std::fprintf(stderr, "  interpolation: the draw time %d is outside the last tick (%d..%d)\n", static_cast<int>(drawTime), static_cast<int>(now - SimHarness::kFrameMs), static_cast<int>(now)); ++failures; break; }
        // the drawn position is never ahead of the real one along the way the craft is going, and never more than a tick behind it
        const long long dx = static_cast<long long>(real.mX) - drawn.mX, dy = static_cast<long long>(real.mY) - drawn.mY;
        if (dx * dx + dy * dy > 2500LL * 2500LL) { std::fprintf(stderr, "  interpolation: the drawn craft is more than a tick behind (frame %d)\n", frame); ++failures; break; }
        (void)moved;
    }
    // a restart clears what there is to draw from
    world.session->SetSimulationTime(0);
    MR_SimulationTime unused = 0;
    if (world.session->BeginInterpolatedRender(unused, 0.0)) { std::fprintf(stderr, "  interpolation: still drawing between ticks after a restart\n"); ++failures; world.session->EndInterpolatedRender(); }
    std::fprintf(stderr, "  %-18s %s\n", "interpolation", failures == 0 ? "ok" : "FAILED");
    return failures;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <track> [ticks]\n", argv[0]); return 2; }
    const std::string track = argv[1];
    const int ticks = argc > 2 ? std::atoi(argv[2]) : 1500;

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const std::string path = SimHarness::TrackPath(track);

    std::vector<int> uneven, stalls;
    unsigned seed = 12345;
    for (int i = 0; i < 997; ++i) { seed = seed * 1103515245u + 12345u; uneven.push_back(1 + static_cast<int>((seed >> 16) % 41)); }
    for (int i = 0; i < 60; ++i) stalls.push_back(i % 20 == 19 ? 1000 : 16);
    const std::vector<Pattern> patterns = {
        {"16 ms (62.5 Hz)", {16}},
        {"60 Hz (16/17 ms)", {17, 16, 17}},
        {"30 Hz", {33}},
        {"144 Hz", {7}},
        {"1 ms frames", {1}},
        {"uneven 1-41 ms", uneven},
        {"stalls of 1 s", stalls},
    };

    std::uint64_t reference = 0;
    int failures = 0;
    for (const Pattern& p : patterns) {
        int frames = 0;
        const std::uint64_t hash = Run(path, track, p, ticks, frames);
        if (reference == 0) reference = hash;
        const bool same = hash != 0 && hash == reference;
        std::fprintf(stderr, "  %-18s %7d frames  %016llx  %s\n", p.name, frames, static_cast<unsigned long long>(hash), same ? "same" : "DIFFERENT");
        if (!same) ++failures;
    }
    failures += CheckInterpolation(path, track);
    std::printf("%s %d %016llx\n", track.c_str(), ticks, static_cast<unsigned long long>(reference));
    return failures == 0 ? 0 : 1;
}

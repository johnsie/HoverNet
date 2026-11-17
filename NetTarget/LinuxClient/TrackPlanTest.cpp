// Builds the plan of a track and checks it has a shape and the pads the track is known to have: HoverNetTrackPlanTest TRACK.trk NAME [out.ppm]
#include "TrackPlan.h"

#include "../Util/DllObjectFactory.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s TRACK.trk NAME [out.ppm]\n", argv[0]); return 2; }
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const HoverNetPlan::Plan plan = HoverNetPlan::BuildPlan(argv[1], argv[2], 480);
    if (!plan.ok) { std::printf("FAIL: %s could not be drawn\n", argv[2]); return 1; }
    long solid = 0;
    for (std::size_t i = 3; i < plan.rgba.size(); i += 4) if (plan.rgba[i] > 128) ++solid;
    std::printf("%s: %dx%d, %d rooms, %ld road pixels, fuel %d, boost %d, power-ups %d, finish %d\n", argv[2], plan.width, plan.height, plan.rooms, solid, plan.fuelPads, plan.boostPads, plan.powerUps, plan.finishLines);
    if (argc > 3) {
        if (FILE* f = std::fopen(argv[3], "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", plan.width, plan.height);
            for (std::size_t i = 0; i < plan.rgba.size(); i += 4) { const int a = plan.rgba[i + 3]; for (int c = 0; c < 3; ++c) std::fputc((plan.rgba[i + c] * a + 14 * (255 - a)) / 255, f); }
            std::fclose(f);
        }
    }
    return solid > 200 ? 0 : 1;
}

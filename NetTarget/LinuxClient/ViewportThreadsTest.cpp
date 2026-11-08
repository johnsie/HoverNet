// ViewportThreadsTest -- the software rasteriser can draw several frames at the same time on different threads.
//
// It used to keep its working state in file-scope variables, so two viewports drawing at once (on two threads) overwrote each other's parameters.
// Here eight different views (camera position and direction) of a textured wall and a textured floor are drawn once on one thread, to get the
// answer, and then many times at once on four threads, each with its own video buffer and viewport; every frame must be identical to the answer.
// Run from the repository root (it loads NetTarget/ObjFac1.dat).
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
std::uint64_t Fnv(const MR_UInt8* data, std::size_t size)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) { h ^= data[i]; h *= 1099511628211ULL; }
    return h;
}

// One frame of one of eight views; returns the hash of the 8-bit picture and the number of pixels drawn.
std::uint64_t Render(const MR_ResBitmap* wall, const MR_ResBitmap* floor, int view, std::size_t* drawn)
{
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(320, 240) || !buffer.Lock()) return 0;
    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, buffer.GetXRes(), buffer.GetYRes(), 800);
    viewport.SetupCameraPosition(MR_3DCoordinate(-150 * view, 60 * view - 200, 1000 + 40 * view), (view - 4) * 50, view - 3);
    viewport.Clear(255);   // 255 is not a colour the textures use, so anything else is a pixel that was drawn
    viewport.ClearZ();
    viewport.RenderWallSurface(MR_3DCoordinate(4000, 2500, 2500), MR_3DCoordinate(4000, -2500, 0), 5000, wall);
    viewport.RenderWallSurface(MR_3DCoordinate(2000 + 200 * view, -3000, 2200), MR_3DCoordinate(9000, -3000, 0), 7000, wall);
    const MR_2DCoordinate quad[4] = {MR_2DCoordinate(1500, -3000), MR_2DCoordinate(12000, -3000), MR_2DCoordinate(12000, 3000), MR_2DCoordinate(1500, 3000)};
    viewport.RenderHorizontalSurface(4, quad, 0, TRUE, floor);
    const std::size_t pixels = static_cast<std::size_t>(buffer.GetXRes()) * buffer.GetYRes();
    if (drawn != nullptr) { *drawn = 0; for (std::size_t i = 0; i < pixels; ++i) if (buffer.GetBuffer()[i] != 255) ++*drawn; }
    return Fnv(buffer.GetBuffer(), pixels);
}
}

int main()
{
    MR_InitTrigoTables();
    MR_ResourceLib resources("NetTarget/ObjFac1.dat");
    const MR_ResBitmap* wall = resources.GetBitmap(1051);
    const MR_ResBitmap* floor = resources.GetBitmap(1051);
    if (wall == nullptr || floor == nullptr) { std::fprintf(stderr, "Could not load the test bitmaps\n"); return 2; }

    const int kViews = 8;
    std::uint64_t answer[kViews];
    for (int v = 0; v < kViews; ++v) {
        std::size_t drawn = 0;
        answer[v] = Render(wall, floor, v, &drawn);
        if (drawn < 3000) { std::fprintf(stderr, "view %d drew only %zu pixels: the test scene is wrong\n", v, drawn); return 2; }
        if (Render(wall, floor, v, nullptr) != answer[v]) { std::fprintf(stderr, "view %d is not even repeatable on one thread\n", v); return 2; }
    }

    const int kThreads = 4, kRounds = 40;
    std::atomic<int> wrong(0), total(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int round = 0; round < kRounds; ++round) {
                const int v = (t + round) % kViews;
                ++total;
                if (Render(wall, floor, v, nullptr) != answer[v]) ++wrong;
            }
        });
    }
    for (std::thread& th : threads) th.join();

    std::printf("%d frames drawn on %d threads at once: %d differ from the single-thread answer\n", total.load(), kThreads, wrong.load());
    if (wrong.load() != 0) { std::printf("viewport threads: FAILED\n"); return 1; }
    std::printf("viewport threads: all frames identical\n");
    return 0;
}

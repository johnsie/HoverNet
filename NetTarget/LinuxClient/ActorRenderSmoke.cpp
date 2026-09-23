#include "../Platform/MfcCompat.h"
#include "../ObjFac1/ObjFac1Res.h"
#include "../ObjFacTools/ResActor.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>

int main()
{
    MR_InitTrigoTables();

    MR_ResourceLib resources("NetTarget/ObjFac1.dat");
    const MR_ResActor* actor = resources.GetActor(MR_PWRUP);
    if (actor == nullptr || actor->GetSequenceCount() == 0 || actor->GetFrameCount(0) == 0) {
        std::fprintf(stderr, "Could not load the power-up actor\n");
        return 1;
    }

    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(640, 480) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not create headless framebuffer\n");
        return 1;
    }

    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, buffer.GetXRes(), buffer.GetYRes(), 6000);
    viewport.SetupCameraPosition(MR_3DCoordinate(0, 0, 1000), 0, 0);
    viewport.Clear(0);
    viewport.ClearZ();

    MR_PositionMatrix matrix;
    if (!viewport.ComputePositionMatrix(matrix, MR_3DCoordinate(4000, 0, 0), 0, 1000)) {
        std::fprintf(stderr, "Power-up actor is outside the viewport\n");
        return 1;
    }
    actor->Draw(&viewport, matrix, 0, 0);

    int nonZeroPixels = 0;
    const int pixelCount = buffer.GetXRes() * buffer.GetYRes();
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) ++nonZeroPixels;
    }
    if (nonZeroPixels == 0) {
        std::fprintf(stderr, "Power-up actor did not rasterize\n");
        return 1;
    }

    std::printf("Actor raster smoke test passed: pixels=%d\n", nonZeroPixels);
    return 0;
}
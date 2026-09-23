#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>

int main()
{
    MR_InitTrigoTables();

    MR_ResourceLib resources("NetTarget/ObjFac1.dat");
    MR_ResBitmap* bitmap = resources.GetBitmap(1051);
    if (bitmap == nullptr) {
        std::fprintf(stderr, "Could not load ClassicH floor bitmap\n");
        return 1;
    }

    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(640, 480) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not create headless framebuffer\n");
        return 1;
    }
    buffer.SetBackPalette(new MR_UInt8[MR_BACK_COLORS * 3]);
    MR_UInt8* replacementPalette = new MR_UInt8[MR_BACK_COLORS * 3];
    buffer.SetBackPalette(replacementPalette);
    if (buffer.GetBackPalette() != replacementPalette) {
        std::fprintf(stderr, "Could not replace the background palette\n");
        return 1;
    }

    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, buffer.GetXRes(), buffer.GetYRes(), 6000);
    viewport.SetupCameraPosition(MR_3DCoordinate(0, 0, 1000), 0, 0);
    viewport.Clear(0);
    viewport.ClearZ();
    viewport.RenderWallSurface(MR_3DCoordinate(4000, 2000, 2500),
                               MR_3DCoordinate(4000, -2000, 0), 4000, bitmap);

    int nonZeroPixels = 0;
    const int pixelCount = buffer.GetXRes() * buffer.GetYRes();
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) ++nonZeroPixels;
    }
    if (nonZeroPixels == 0) {
        std::fprintf(stderr, "Viewport did not rasterize a textured wall\n");
        return 1;
    }

    std::printf("Viewport raster smoke test passed: pixels=%d\n", nonZeroPixels);
    return 0;
}
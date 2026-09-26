#include "../Game2/ClientSession.h"
#include "../MainCharacter/MainCharacter.h"
#include "../MainCharacter/MainCharacterRenderer.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>

int main()
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead("NetTarget/Tracks/ClassicH.trk")) {
        std::fprintf(stderr, "Could not open ClassicH.trk\n");
        delete track;
        return 1;
    }

    MR_ClientSession session;
    if (!session.LoadNew("ClassicH", track, 1, FALSE, nullptr) || !session.CreateMainCharacter()) {
        std::fprintf(stderr, "Could not create a local ClassicH player\n");
        return 1;
    }

    MR_MainCharacter* player = session.GetMainCharacter();
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (player == nullptr || !buffer.SetVideoMode(640, 480) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not prepare the player render test\n");
        return 1;
    }

    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, buffer.GetXRes(), buffer.GetYRes(), 800);
    MR_3DCoordinate camera = player->mPosition;
    camera.mX -= 3400 * MR_Cos[player->mOrientation] / MR_TRIGO_FRACT;
    camera.mY -= 3400 * MR_Sin[player->mOrientation] / MR_TRIGO_FRACT;
    camera.mZ += 1700;
    viewport.SetupCameraPosition(camera, player->mOrientation, 0);
    viewport.Clear(0);
    viewport.ClearZ();
    player->Render(&viewport, session.GetSimulationTime());

    int actorPixels = 0;
    const int pixelCount = buffer.GetXRes() * buffer.GetYRes();
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++actorPixels;
        }
    }
    if (actorPixels == 0) {
        std::fprintf(stderr, "ObjFac1 did not render the main-character actor\n");
        return 1;
    }

    player->SetHoverModel(1);
    viewport.Clear(0);
    viewport.ClearZ();
    player->Render(&viewport, session.GetSimulationTime());

    int hiTechPixels = 0;
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++hiTechPixels;
        }
    }
    if (hiTechPixels == 0) {
        std::fprintf(stderr, "ObjFac1 did not render the HiTech main-character actor\n");
        return 1;
    }

    player->SetHoverModel(4);
    viewport.Clear(0);
    viewport.ClearZ();
    player->Render(&viewport, session.GetSimulationTime());

    int mantaPixels = 0;
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++mantaPixels;
        }
    }
    if (mantaPixels == 0) {
        std::fprintf(stderr, "ObjFac1 did not render the Manta main-character actor\n");
        return 1;
    }

    // Regression: the Manta's powered sequence contains flame overlays. The
    // Linux renderer must compose those over sequence 0, not replace the hull.
    viewport.Clear(0);
    viewport.ClearZ();
    MR_ObjectFromFactoryId rendererId = {1, 100};
    MR_MainCharacterRenderer* poweredRenderer = static_cast<MR_MainCharacterRenderer*>(
        MR_DllObjectFactory::CreateObject(rendererId));
    if (poweredRenderer == nullptr) {
        std::fprintf(stderr, "Could not create powered-Manta renderer\n");
        return 1;
    }
    poweredRenderer->Render(&viewport, player->mPosition, player->mOrientation,
                            TRUE, 0, 24);
    delete poweredRenderer;

    int poweredMantaPixels = 0;
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++poweredMantaPixels;
        }
    }
    if (poweredMantaPixels < mantaPixels / 2) {
        std::fprintf(stderr,
                     "Powered Manta lost its hull: idle=%d powered=%d\n",
                     mantaPixels, poweredMantaPixels);
        return 1;
    }
    if (poweredMantaPixels <= mantaPixels + 100) {
        std::fprintf(stderr,
                     "Powered Manta flames were not visibly rendered: idle=%d powered=%d\n",
                     mantaPixels, poweredMantaPixels);
        return 1;
    }

    std::printf("Main-character actor render smoke test passed: pixels=%d hiTech=%d manta=%d powered=%d\n",
                actorPixels, hiTechPixels, mantaPixels, poweredMantaPixels);
    return 0;
}

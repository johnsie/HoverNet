// Regression test for the "in-race chat never appears on screen" bug: the chat
// message stack (MR_ClientSession::AddMessage/GetMessageStack) was only ever
// drawn by code inside Observer.cpp's "DISABLED SECTION" -- a commented-out block
// inside Render3DView that the live rendering path (RenderNormalDisplay) never
// called into. Network delivery and message-stack storage were both already
// correct (see InRaceChatSmoke.cpp); nothing on either platform actually painted
// the result onto the screen. This test renders the same frame twice, once
// with an empty message stack and once with a message added, at a frozen
// simulation time (so the animated 3D scene behind it can't itself account for
// any difference), and checks that a real number of pixels changed -- proof the
// chat overlay is actually being drawn, not just stored.
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>
#include <vector>

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

    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(640, 480) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not create a framebuffer\n");
        return 1;
    }

    MR_ClientSession session;
    if (!session.LoadNew("ClassicH", track, 1, TRUE, &buffer) || !session.CreateMainCharacter()) {
        std::fprintf(stderr, "Could not create a local ClassicH session\n");
        return 1;
    }
    MR_MainCharacter* player = session.GetMainCharacter();
    session.SetSimulationTime(5000);

    MR_Observer* observer = MR_Observer::New();
    if (observer == nullptr) {
        std::fprintf(stderr, "Could not create an observer\n");
        return 1;
    }

    const int pixelCount = buffer.GetXRes() * buffer.GetYRes();

    observer->RenderNormalDisplay(&buffer, &session, player, session.GetSimulationTime(), session.GetBackImage());
    std::vector<MR_UInt8> beforeFrame(buffer.GetBuffer(), buffer.GetBuffer() + pixelCount);

    session.AddMessage("Player 2: this should be visible on screen");

    observer->RenderNormalDisplay(&buffer, &session, player, session.GetSimulationTime(), session.GetBackImage());
    std::vector<MR_UInt8> afterFrame(buffer.GetBuffer(), buffer.GetBuffer() + pixelCount);

    int changedPixels = 0;
    for (int i = 0; i < pixelCount; ++i) {
        if (beforeFrame[i] != afterFrame[i]) {
            ++changedPixels;
        }
    }

    // A short line of text at this font scale touches at least a few hundred
    // pixels; a handful of stray differences wouldn't be meaningful, but this
    // threshold is nowhere near what could happen by chance.
    constexpr int kMinChangedPixels = 100;
    if (changedPixels < kMinChangedPixels) {
        std::fprintf(stderr,
                     "In-race chat message did not visibly render: only %d pixels changed (need >= %d)\n",
                     changedPixels, kMinChangedPixels);
        observer->Delete();
        return 1;
    }

    std::printf("ObserverChatRenderSmoke passed: chat message changed %d pixels on screen\n", changedPixels);
    observer->Delete();
    return 0;
}

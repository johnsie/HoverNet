#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"

#include <cstdio>

int main()
{
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead("NetTarget/Tracks/ClassicH.trk")) {
        std::fprintf(stderr, "Could not open ClassicH.trk\n");
        delete track;
        return 1;
    }

    MR_GameSession session(FALSE);
    if (!session.LoadNew("ClassicH", track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load ClassicH.trk into a game session\n");
        return 1;
    }

    const MR_Level* level = session.GetCurrentLevel();
    if (level->GetRoomCount() <= 0 || level->GetPlayerCount() <= 0) {
        std::fprintf(stderr, "ClassicH.trk did not produce a valid level\n");
        return 1;
    }

    std::puts("ClassicH track load smoke test passed");
    return 0;
}
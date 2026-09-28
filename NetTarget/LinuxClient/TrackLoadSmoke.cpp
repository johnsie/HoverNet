#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    const char* trackPath = argc > 1 ? argv[1] : "NetTarget/Tracks/ClassicH.trk";
    const char* trackName = argc > 2 ? argv[2] : "ClassicH";
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(trackPath)) {
        std::fprintf(stderr, "Could not open %s\n", trackPath);
        delete track;
        return 1;
    }

    MR_GameSession session(FALSE);
    if (!session.LoadNew(trackName, track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load %s into a game session\n", trackPath);
        return 1;
    }

    const MR_Level* level = session.GetCurrentLevel();
    if (level->GetRoomCount() <= 0 || level->GetPlayerCount() <= 0) {
        std::fprintf(stderr, "%s did not produce a valid level\n", trackPath);
        return 1;
    }

    for (int room = 0; room < level->GetRoomCount(); ++room) {
        long long twiceArea = 0;
        const int vertices = level->GetRoomVertexCount(room);
        for (int vertex = 0; vertex < vertices; ++vertex) {
            const MR_2DCoordinate& first = level->GetRoomVertex(room, vertex);
            const MR_2DCoordinate& second = level->GetRoomVertex(room, (vertex + 1) % vertices);
            twiceArea += static_cast<long long>(first.mX) * second.mY -
                static_cast<long long>(second.mX) * first.mY;
        }
        if (twiceArea >= 0) {
            std::fprintf(stderr, "%s room %d is not clockwise\n", trackPath, room);
            return 1;
        }
        if (std::getenv("HOVERNET_TRACK_TRACE") != nullptr) {
            std::printf("room=%d signed_area2=%lld floor=%d ceiling=%d features=%d\n",
                room, twiceArea, level->GetRoomBottomLevel(room), level->GetRoomTopLevel(room),
                level->GetFeatureCount(room));
            std::printf(" walls=");
            for (int wall = 0; wall < level->GetRoomVertexCount(room); ++wall) {
                const MR_ObjectFromFactoryId type = level->GetRoomWallElement(room, wall)->GetTypeId();
                std::printf("%s%u", wall == 0 ? "" : ",", static_cast<unsigned>(type.mClassId));
            }
            std::puts("");
            for (int child = 0; child < level->GetFeatureCount(room); ++child) {
                const int feature = level->GetFeature(room, child);
                const MR_ObjectFromFactoryId top = level->GetFeatureTopElement(feature)->GetTypeId();
                const MR_ObjectFromFactoryId bottom = level->GetFeatureBottomElement(feature)->GetTypeId();
                std::printf(" feature=%d bottom=%d top=%d bottom_texture=%u top_texture=%u vertices=",
                    feature, level->GetFeatureBottomLevel(feature), level->GetFeatureTopLevel(feature),
                    static_cast<unsigned>(bottom.mClassId), static_cast<unsigned>(top.mClassId));
                for (int vertex = 0; vertex < level->GetFeatureVertexCount(feature); ++vertex) {
                    const MR_2DCoordinate& point = level->GetFeatureVertex(feature, vertex);
                    std::printf("%s(%d,%d)", vertex == 0 ? "" : ",", point.mX, point.mY);
                }
                std::puts("");
            }
        }
    }

    std::printf("%s track load smoke test passed\n", trackName);
    return 0;
}

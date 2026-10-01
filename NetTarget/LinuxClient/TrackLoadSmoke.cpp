#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <queue>
#include <set>
#include <string>
#include <vector>

// Strict mode is the 2.0 authoring contract for bundled tracks. --community is
// the compatibility tier for third-party tracks made with the classic tools: it
// only rejects a track the game cannot load or start a race on, and downgrades
// everything else (missing race gates, disconnected rooms, odd geometry, ...) to
// a warning, because the original game ran those tracks. A track without a
// complete finish/checkpoint set is reported as "freeplay" rather than "race".
int main(int argc, char** argv)
{
    bool community = false;
    bool manifest = false;
    std::vector<const char*> positional;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--community") community = true;
        else if (argument == "--manifest") manifest = true;
        else positional.push_back(argv[index]);
    }
    if (positional.empty() || positional.size() > 2) {
        std::fprintf(stderr, "Usage: HoverNetTrackValidator [--community] [--manifest] TRACK.trk [TRACK_NAME]\n");
        return 2;
    }

    const char* trackPath = positional[0];
    const char* trackName = positional.size() > 1 ? positional[1] : positional[0];
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(trackPath)) {
        std::fprintf(stderr, "ERROR %s: cannot open track\n", trackPath);
        delete track;
        return 1;
    }

    MR_GameSession session(FALSE);
    MR_Level::ResetSerializationFaultCount();
    if (!session.LoadNew(trackName, track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "ERROR %s: cannot load track into a game session\n", trackPath);
        return 1;
    }

    const MR_Level* level = session.GetCurrentLevel();
    const int roomCount = level->GetRoomCount();
    const int playerCount = level->GetPlayerCount();
    int errors = 0;
    int warnings = 0;
    // Always an error: the track cannot be played.
    auto error = [&](const std::string& message) {
        std::fprintf(stderr, "ERROR %s: %s\n", trackPath, message.c_str());
        ++errors;
    };
    // An error under the strict 2.0 contract, only a warning for community tracks.
    auto strict = [&](const std::string& message) {
        if (!community) {
            error(message);
            return;
        }
        std::fprintf(stderr, "WARN %s: %s\n", trackPath, message.c_str());
        ++warnings;
    };

    if (MR_Level::GetSerializationFaultCount() != 0) {
        error("track data is damaged or uses unsupported objects (" +
              std::to_string(MR_Level::GetSerializationFaultCount()) + " sections could not be read)");
    }

    if (roomCount <= 0) error("track has no rooms");
    if (playerCount < (community ? 1 : 2)) error("track needs at least " + std::string(community ? "one start" : "two multiplayer starts"));
    else if (playerCount < 2) strict("track has a single start; extra players will share it");
    if (playerCount > MR_NB_MAX_PLAYER) error("track exceeds the supported player limit");

    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(std::max(0, roomCount)));
    for (int room = 0; room < roomCount; ++room) {
        const int vertices = level->GetRoomVertexCount(room);
        if (vertices < 3) {
            strict("room " + std::to_string(room) + " has fewer than three vertices");
            continue;
        }
        if (level->GetRoomBottomLevel(room) >= level->GetRoomTopLevel(room)) {
            strict("room " + std::to_string(room) + " has no positive vertical clearance");
        }
        long long twiceArea = 0;
        for (int wall = 0; wall < vertices; ++wall) {
            const MR_2DCoordinate& first = level->GetRoomVertex(room, wall);
            const MR_2DCoordinate& second = level->GetRoomVertex(room, (wall + 1) % vertices);
            twiceArea += static_cast<long long>(first.mX) * second.mY -
                static_cast<long long>(second.mX) * first.mY;
            if (first.mX == second.mX && first.mY == second.mY) {
                strict("room " + std::to_string(room) + " has a zero-length collision wall");
            }
            const int neighbor = level->GetNeighbor(room, wall);
            if (neighbor >= 0) {
                if (neighbor >= roomCount || neighbor == room) {
                    error("room " + std::to_string(room) + " has an invalid neighbor");
                }
                else {
                    adjacency[static_cast<std::size_t>(room)].push_back(neighbor);
                    bool reciprocal = false;
                    for (int otherWall = 0; otherWall < level->GetRoomVertexCount(neighbor); ++otherWall) {
                        if (level->GetNeighbor(neighbor, otherWall) == room) reciprocal = true;
                    }
                    if (!reciprocal) {
                        strict("room " + std::to_string(room) + " has a one-way connection to room " +
                               std::to_string(neighbor));
                    }
                }
            }
        }
        if (twiceArea >= 0) strict("room " + std::to_string(room) + " is not clockwise");
        if (twiceArea == 0) strict("room " + std::to_string(room) + " has zero collision area");
    }

    if (roomCount > 0) {
        std::vector<bool> reached(static_cast<std::size_t>(roomCount), false);
        std::queue<int> pending;
        reached[0] = true; pending.push(0);
        while (!pending.empty()) {
            const int room = pending.front(); pending.pop();
            for (int neighbor : adjacency[static_cast<std::size_t>(room)]) {
                if (!reached[static_cast<std::size_t>(neighbor)]) {
                    reached[static_cast<std::size_t>(neighbor)] = true; pending.push(neighbor);
                }
            }
        }
        for (int room = 0; room < roomCount; ++room) {
            if (!reached[static_cast<std::size_t>(room)]) {
                strict("room " + std::to_string(room) + " is disconnected from the race topology");
            }
        }
    }

    std::set<std::pair<MR_Int32, MR_Int32>> starts;
    for (int player = 0; player < playerCount; ++player) {
        const int room = level->GetStartingRoom(player);
        const MR_3DCoordinate& position = level->GetStartingPos(player);
        if (room < 0 || room >= roomCount) {
            error("start " + std::to_string(player) + " references an invalid room");
            continue;
        }
        if (!starts.insert({position.mX, position.mY}).second) {
            strict("start " + std::to_string(player) + " overlaps another start");
        }
        MR_2DCoordinate point; point.mX = position.mX; point.mY = position.mY;
        if (level->FindRoomForPoint(point, room) != room) {
            error("start " + std::to_string(player) + " is outside its declared room");
        }
        if (position.mZ < level->GetRoomBottomLevel(room) ||
            position.mZ >= level->GetRoomTopLevel(room)) {
            strict("start " + std::to_string(player) + " is outside room vertical bounds");
        }
    }

    int finishCount = 0, checkpoint1Count = 0, checkpoint2Count = 0, elementCount = 0;
    for (int room = -1; room < roomCount; ++room) {
        for (MR_FreeElementHandle handle = level->GetFirstFreeElement(room); handle != nullptr;
             handle = MR_Level::GetNextFreeElement(handle)) {
            MR_FreeElement* element = MR_Level::GetFreeElement(handle);
            if (element == nullptr) { error("free-element list contains a null element"); continue; }
            ++elementCount;
            const unsigned classId = static_cast<unsigned>(element->GetTypeId().mClassId);
            if (std::getenv("HOVERNET_TRACK_TRACE") != nullptr) {
                std::printf("element room=%d class=%u x=%d y=%d z=%d\n", room, classId,
                            element->mPosition.mX, element->mPosition.mY, element->mPosition.mZ);
            }
            if (classId == 202) ++finishCount;
            else if (classId == 203) ++checkpoint1Count;
            else if (classId == 204) ++checkpoint2Count;
        }
    }
    const bool hasRaceGates = finishCount >= 1 && checkpoint1Count >= 1 && checkpoint2Count >= 1;
    if (finishCount < 1) strict("track must contain a finish line");
    if (checkpoint1Count < 1) strict("track must contain checkpoint 1");
    if (checkpoint2Count < 1) strict("track must contain checkpoint 2");

    if (errors != 0) {
        std::fprintf(stderr, "FAILED %s: %d validation error(s)\n", trackPath, errors);
        return 1;
    }
    std::printf("VALID %s rooms=%d starts=%d elements=%d race_gates=%d mode=%s warnings=%d\n",
                trackName, roomCount, playerCount, elementCount,
                finishCount + checkpoint1Count + checkpoint2Count, hasRaceGates ? "race" : "freeplay",
                warnings);
    if (manifest) {
        std::printf("MANIFEST\t%s\t%s\t%d\t%d\t%d\n", trackName, hasRaceGates ? "race" : "freeplay",
                    playerCount, roomCount, warnings);
    }
    return 0;
}

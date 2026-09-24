#include "../Game2/ClientSession.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>

int main()
{
    MR_MainCharacter::RegisterFactory();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead("NetTarget/Tracks/ClassicH.trk")) {
        std::fprintf(stderr, "Could not open ClassicH.trk\n");
        delete track;
        return 1;
    }

    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(320, 200) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not create a framebuffer\n");
        return 1;
    }

    MR_ClientSession session;
    if (!session.LoadNew("ClassicH", track, 1, FALSE, &buffer) ||
        !session.CreateMainCharacter()) {
        std::fprintf(stderr, "Could not create a local ClassicH session\n");
        return 1;
    }

    MR_MainCharacter* player = session.GetMainCharacter();
    const MR_Level* level = session.GetCurrentLevel();
    if (player == nullptr || level == nullptr ||
        player->mRoom != level->GetStartingRoom(0)) {
        std::fprintf(stderr, "Local player was not inserted into the starting room\n");
        return 1;
    }
    if (level->GetPlayerCount() < 2 || !session.PlaceCharacterAtStart(player, 1) ||
        player->mRoom != level->GetStartingRoom(1) ||
        player->mPosition.mX != level->GetStartingPos(1).mX ||
        player->mPosition.mY != level->GetStartingPos(1).mY ||
        player->mPosition.mZ != level->GetStartingPos(1).mZ) {
        std::fprintf(stderr, "Local player was not moved to multiplayer start slot 1\n");
        return 1;
    }
    if (player->mPosition.mX == level->GetStartingPos(0).mX &&
        player->mPosition.mY == level->GetStartingPos(0).mY &&
        player->mPosition.mZ == level->GetStartingPos(0).mZ) {
        std::fprintf(stderr, "ClassicH multiplayer start slots overlap\n");
        return 1;
    }
    if (session.GetBackImage() == nullptr || buffer.GetBackPalette() == nullptr ||
        session.GetMap() == nullptr) {
        std::fprintf(stderr, "ClassicH presentation data was not loaded\n");
        return 1;
    }

    MR_MainCharacter* remote = MR_MainCharacter::New(1, FALSE);
    remote->mPosition = level->GetStartingPos(0);
    remote->SetOrientation(level->GetStartingOrientation(0));
    remote->SetHoverId(1);
    if (session.InsertRemoteCharacter(remote, level->GetStartingRoom(0)) == nullptr) {
        delete remote;
        std::fprintf(stderr, "Remote player was not inserted\n");
        return 1;
    }
    if (session.GetNbPlayers() != 2 || session.GetPlayer(0) != player ||
        session.GetPlayer(1) != remote) {
        std::fprintf(stderr, "Remote player was not exposed to HUD enumeration\n");
        return 1;
    }

    session.SetControlState(MR_MainCharacter::eMotorOn | MR_MainCharacter::eRight, 0);
    session.Process();
    std::printf("ClientSession smoke test passed: room=%d hover=%d\n",
                player->mRoom, player->GetHoverId());
    return 0;
}
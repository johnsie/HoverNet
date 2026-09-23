#include "../MainCharacter/MainCharacter.h"
#include "../Model/FreeElementMovingHelper.h"
#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"

#include <cmath>
#include <cstdio>

class PlayerMovementCylinder : public MR_CylinderShape
{
public:
    PlayerMovementCylinder(MR_Int32 axisX, MR_Int32 axisY, MR_Int32 zMin)
        : mAxisX(axisX), mAxisY(axisY), mZMin(zMin) {}

    MR_Int32 AxisX() const override { return mAxisX; }
    MR_Int32 AxisY() const override { return mAxisY; }
    MR_Int32 RayLen() const override { return 1100; }
    MR_Int32 ZMin() const override { return mZMin; }
    MR_Int32 ZMax() const override { return mZMin + 1500; }

private:
    MR_Int32 mAxisX;
    MR_Int32 mAxisY;
    MR_Int32 mZMin;
};

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

    MR_GameSession session(FALSE);
    if (!session.LoadNew("ClassicH", track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load ClassicH.trk\n");
        return 1;
    }

    MR_Level* level = session.GetCurrentLevel();
    const int startRoom = level->GetStartingRoom(0);
    MR_MainCharacter* player = MR_MainCharacter::New(5, TRUE);
    if (player == nullptr || startRoom < 0 || startRoom >= level->GetRoomCount()) {
        std::fprintf(stderr, "Could not create a player for ClassicH\n");
        delete player;
        return 1;
    }

    player->mPosition = level->GetStartingPos(0);
    player->SetOrientation(level->GetStartingOrientation(0));
    player->mRoom = startRoom;
    const MR_FreeElementHandle handle = level->InsertElement(player, startRoom, FALSE);
    if (handle == nullptr || level->GetFreeElement(handle) != player) {
        std::fprintf(stderr, "Could not insert player into ClassicH\n");
        return 1;
    }

    const int initialModel = player->GetHoverModel();
    player->SetControlState(MR_MainCharacter::eRight, -1);
    if (player->GetHoverModel() != (initialModel + 1) % 4) {
        std::fprintf(stderr, "Pre-race right control did not select the next hovercraft\n");
        return 1;
    }
    player->SetControlState(0, -1);
    player->SetControlState(MR_MainCharacter::eLeft, -1);
    if (player->GetHoverModel() != initialModel) {
        std::fprintf(stderr, "Pre-race left control did not select the previous hovercraft\n");
        return 1;
    }

    int solidWall = -1;
    for (int wall = 0; wall < level->GetRoomVertexCount(startRoom); ++wall) {
        if (level->GetNeighbor(startRoom, wall) == -1) {
            solidWall = wall;
            break;
        }
    }
    if (solidWall == -1) {
        std::fprintf(stderr, "ClassicH starting room has no structural wall\n");
        return 1;
    }

    const int nextWallVertex = (solidWall + 1) % level->GetRoomVertexCount(startRoom);
    const MR_2DCoordinate& wallStart = level->GetRoomVertex(startRoom, solidWall);
    const MR_2DCoordinate& wallEnd = level->GetRoomVertex(startRoom, nextWallVertex);
    const MR_Int32 wallX = (wallStart.mX + wallEnd.mX) / 2;
    const MR_Int32 wallY = (wallStart.mY + wallEnd.mY) / 2;
    const double towardStartX = level->GetStartingPos(0).mX - wallX;
    const double towardStartY = level->GetStartingPos(0).mY - wallY;
    const double towardStartLength = std::sqrt(towardStartX * towardStartX + towardStartY * towardStartY);
    if (towardStartLength == 0.0) {
        std::fprintf(stderr, "Could not position a player-sized wall probe\n");
        return 1;
    }

    PlayerMovementCylinder wallProbe(
        wallX + static_cast<MR_Int32>(towardStartX * 200 / towardStartLength),
        wallY + static_cast<MR_Int32>(towardStartY * 200 / towardStartLength),
        level->GetRoomBottomLevel(startRoom));
    MR_ObstacleCollisionReport wallReport;
    wallReport.GetContactWithObstacles(level, &wallProbe, startRoom, nullptr, TRUE);
    if (!wallReport.IsInMaze() || !wallReport.HaveContact()) {
        std::fprintf(stderr, "Player-sized probe did not detect a structural wall\n");
        return 1;
    }

    MR_3DCoordinate startPosition = level->GetStartingPos(0);
    startPosition.mX = wallX + static_cast<MR_Int32>(towardStartX * 10000 / towardStartLength);
    startPosition.mY = wallY + static_cast<MR_Int32>(towardStartY * 10000 / towardStartLength);
    if (level->FindRoomForPoint(MR_2DCoordinate(startPosition.mX, startPosition.mY), startRoom) != startRoom) {
        std::fprintf(stderr, "Could not place a player inside the structural wall\n");
        return 1;
    }
    const MR_Angle wallHeading = RAD_2_MR_ANGLE(std::atan2(wallY - startPosition.mY,
                                                            wallX - startPosition.mX));
    player->mPosition = startPosition;
    player->mRoom = startRoom;
    player->SetOrientation(wallHeading);
    player->SetControlState(MR_MainCharacter::eMotorOn, 0);
    session.SetSimulationTime(6000);
    int wallCollisionRoom = startRoom;
    for (int slice = 0; slice < 500; ++slice) {
        session.SimulateLateElement(handle, 10, wallCollisionRoom);
        wallCollisionRoom = player->mRoom;
    }
    const double distanceMoved = std::sqrt(
        static_cast<double>(player->mPosition.mX - startPosition.mX) *
            (player->mPosition.mX - startPosition.mX) +
        static_cast<double>(player->mPosition.mY - startPosition.mY) *
            (player->mPosition.mY - startPosition.mY));
    const int finalRoom = level->FindRoomForPoint(
        MR_2DCoordinate(player->mPosition.mX, player->mPosition.mY), startRoom);
    if (distanceMoved == 0.0 || wallCollisionRoom != startRoom || finalRoom != startRoom) {
        std::fprintf(stderr, "Player crossed a structural wall: room=%d pointRoom=%d distance=%.0f position=(%d,%d,%d)\n",
                     wallCollisionRoom, finalRoom, distanceMoved,
                     player->mPosition.mX, player->mPosition.mY, player->mPosition.mZ);
        return 1;
    }

    std::printf("MainCharacter smoke test passed: room=%d position=(%d,%d,%d)\n",
                player->mRoom, player->mPosition.mX, player->mPosition.mY, player->mPosition.mZ);
    return 0;
}
#define protected public
#include "../MainCharacter/MainCharacter.h"
#undef protected
#include "../Model/FreeElementMovingHelper.h"
#include "../Model/GameSession.h"
#include "../Model/RaceEffects.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

    MR_MainCharacter* hitSource = MR_MainCharacter::New(5, TRUE);
    MR_MainCharacter* replicatedVictim = MR_MainCharacter::New(5, TRUE);
    if (hitSource == nullptr || replicatedVictim == nullptr) {
        std::fprintf(stderr, "Could not create missile-hit replication players\n");
        delete hitSource;
        delete replicatedVictim;
        return 1;
    }
    hitSource->mPosition = level->GetStartingPos(0);
    hitSource->mRoom = startRoom;
    hitSource->SetOrientation(level->GetStartingOrientation(0));
    MR_LostOfControl missileHit;
    missileHit.mType = MR_LostOfControl::eMissile;
    missileHit.mElementId = -1;
    missileHit.mHoverId = 1;
    hitSource->ApplyEffect(&missileHit, 0, 0, TRUE, 0, 0, 0, level);
    const MR_ElementNetState hitState = hitSource->GetNetState();
    replicatedVictim->SetAsSlave();
    replicatedVictim->SetNetState(hitState.mDataLen, hitState.mData);
    const MR_Angle orientationBeforeHitReaction = replicatedVictim->mOrientation;
    replicatedVictim->Simulate(10, level, startRoom);
    if (replicatedVictim->mOrientation == orientationBeforeHitReaction) {
        std::fprintf(stderr, "Replicated missile hit did not spin the remote craft\n");
        delete hitSource;
        delete replicatedVictim;
        return 1;
    }
    delete hitSource;
    delete replicatedVictim;

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

    // The wall segment's true normal (perpendicular to it, not the start-to-wall
    // vector used above, which mixes normal and along-the-wall components unless
    // the wall happens to be perpendicular to that vector). Bounce reflects the
    // normal component of velocity and preserves the tangential one -- a craft
    // sliding along the wall after bouncing is correct, so only the normal
    // component is a meaningful "did it bounce" signal.
    double wallNormalX = -(wallEnd.mY - wallStart.mY);
    double wallNormalY = (wallEnd.mX - wallStart.mX);
    const double wallNormalLength = std::sqrt(wallNormalX * wallNormalX + wallNormalY * wallNormalY);
    if (wallNormalLength == 0.0) {
        std::fprintf(stderr, "Structural wall has zero length\n");
        return 1;
    }
    wallNormalX /= wallNormalLength;
    wallNormalY /= wallNormalLength;
    if (wallNormalX * towardStartX + wallNormalY * towardStartY < 0.0) {
        wallNormalX = -wallNormalX;
        wallNormalY = -wallNormalY;
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

    // Track how far the player is from the wall along its true normal each slice.
    // Once bounce works, the craft slides along the wall (tangentially) while its
    // normal-direction distance should only ever increase after first contact --
    // it must never go negative (clipped through) or stay pinned at the minimum
    // (stopped dead, no bounce). Continued thrust in the original heading means it
    // can coast back toward the wall's normal *and* slide far enough tangentially
    // to reach some other, unrelated opening in the room -- that's why this only
    // checks the closest approach and the rebound after it, not "stayed in room".
    double closestApproach = towardStartLength;  // Starting distance from the wall midpoint
    double afterApproachMax = 0.0;
    double minProjectedEver = towardStartLength;
    bool pastClosestApproach = false;

    // Short enough to capture the bounce-and-rebound arc without running long
    // enough for continued thrust to slide the craft into an unrelated doorway.
    for (int slice = 0; slice < 85; ++slice) {
        session.SimulateLateElement(handle, 10, wallCollisionRoom);
        wallCollisionRoom = player->mRoom;

        const double projected = (player->mPosition.mX - wallX) * wallNormalX +
                                  (player->mPosition.mY - wallY) * wallNormalY;
        if (std::getenv("SMOKE_TRACE") != nullptr) {
            std::fprintf(stderr, "slice=%d room=%d pos=(%d,%d) projected=%.0f\n", slice, player->mRoom,
                         player->mPosition.mX, player->mPosition.mY, projected);
        }
        minProjectedEver = std::min(minProjectedEver, projected);

        if (!pastClosestApproach) {
            if (projected <= closestApproach) {
                closestApproach = projected;
            }
            else {
                // Distance from the wall started increasing again -- past the point
                // of closest approach, now track how far it bounces back out.
                pastClosestApproach = true;
                afterApproachMax = projected;
            }
        }
        else if (projected > afterApproachMax) {
            afterApproachMax = projected;
        }
    }
    const double distanceMoved = std::sqrt(
        static_cast<double>(player->mPosition.mX - startPosition.mX) *
            (player->mPosition.mX - startPosition.mX) +
        static_cast<double>(player->mPosition.mY - startPosition.mY) *
            (player->mPosition.mY - startPosition.mY));
    if (distanceMoved == 0.0 || minProjectedEver < 0.0) {
        std::fprintf(stderr, "Player clipped through the structural wall: minProjected=%.0f distance=%.0f "
                     "position=(%d,%d,%d)\n", minProjectedEver, distanceMoved,
                     player->mPosition.mX, player->mPosition.mY, player->mPosition.mZ);
        return 1;
    }

    // A craft driving straight into a wall must bounce off it (deflect away, not
    // just stop dead) -- see MainCharacter::ApplyEffect's MR_InertialMoment-based
    // reflection, which only fires when the wall's surface element actually
    // advertises an MR_PhysicalCollision effect (HeadlessBitmapSurface::GetEffectList
    // in the Linux ObjFac1 plugin).
    const double bounceDistance = afterApproachMax - closestApproach;
    if (!pastClosestApproach || bounceDistance < 50.0) {
        std::fprintf(stderr,
                     "Player did not bounce off the wall: closest=%.0f afterMax=%.0f bounce=%.0f\n",
                     closestApproach, afterApproachMax, bounceDistance);
        return 1;
    }

    std::printf("MainCharacter smoke test passed: room=%d position=(%d,%d,%d) bounce=%.0f\n",
                player->mRoom, player->mPosition.mX, player->mPosition.mY, player->mPosition.mZ,
                bounceDistance);
    return 0;
}
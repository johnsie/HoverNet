#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "../Model/GameSession.h"
#include "../Model/MazeElement.h"
#include "../Model/PhysicalCollision.h"
#include "../ObjFacTools/SpriteHandle.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdio>
#include <string>

#ifndef HOVERNET_SOURCE_DIR
#define HOVERNET_SOURCE_DIR "."
#endif

int main()
{
    MR_InitTrigoTables();
    const MR_ObjectFromFactoryId id = {1, 51};
    MR_ObjectFromFactory* object = MR_DllObjectFactory::CreateObject(id);
    if (object == nullptr || !(object->GetTypeId() == id)) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create the expected object\n");
        return 1;
    }
    delete object;

    const MR_ObjectFromFactoryId powerUpId = {1, 152};
    MR_FreeElement* powerUp = dynamic_cast<MR_FreeElement*>(MR_DllObjectFactory::CreateObject(powerUpId));
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (powerUp == nullptr || !buffer.SetVideoMode(640, 480) || !buffer.Lock()) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create a renderable power-up\n");
        delete powerUp;
        return 1;
    }
    powerUp->mPosition = MR_3DCoordinate(4000, 0, 0);
    powerUp->mOrientation = 0;
    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, buffer.GetXRes(), buffer.GetYRes(), 6000);
    viewport.SetupCameraPosition(MR_3DCoordinate(0, 0, 1000), 0, 0);
    viewport.Clear(0);
    viewport.ClearZ();
    powerUp->Render(&viewport, 0);
    delete powerUp;

    int powerUpPixels = 0;
    const int pixelCount = buffer.GetXRes() * buffer.GetYRes();
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++powerUpPixels;
        }
    }
    if (powerUpPixels == 0) {
        std::fprintf(stderr, "Dynamic ObjFac1 power-up did not render\n");
        return 1;
    }

    const MR_ObjectFromFactoryId bumperGateId = {1, 170};
    MR_FreeElement* bumperGate = dynamic_cast<MR_FreeElement*>(MR_DllObjectFactory::CreateObject(bumperGateId));
    if (bumperGate == nullptr) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create a bumper gate\n");
        return 1;
    }
    bumperGate->mPosition = MR_3DCoordinate(5000, 0, 0);
    bumperGate->mOrientation = 0;
    viewport.Clear(0);
    viewport.ClearZ();
    bumperGate->Render(&viewport, 0);
    delete bumperGate;

    int bumperGatePixels = 0;
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++bumperGatePixels;
        }
    }
    if (bumperGatePixels == 0) {
        std::fprintf(stderr, "Dynamic ObjFac1 bumper gate did not render\n");
        return 1;
    }

    const MR_ObjectFromFactoryId missileId = {1, 150};
    MR_FreeElement* missile = dynamic_cast<MR_FreeElement*>(MR_DllObjectFactory::CreateObject(missileId));
    if (missile == nullptr) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create a missile\n");
        return 1;
    }
    missile->mPosition = MR_3DCoordinate(5000, 0, 1100);
    missile->mOrientation = 0;

    {
        MR_RecordFile* track = new MR_RecordFile;
        MR_GameSession session(FALSE);
        const std::string trackPath = std::string(HOVERNET_SOURCE_DIR) + "/NetTarget/Tracks/ClassicH.trk";
        if (!track->OpenForRead(trackPath.c_str()) || !session.LoadNew("ClassicH", track) ||
            session.GetCurrentLevel() == nullptr) {
            std::fprintf(stderr, "Could not load ClassicH for missile collision simulation\n");
            delete missile;
            return 1;
        }
        MR_Level* level = session.GetCurrentLevel();
        const int startRoom = level->GetStartingRoom(0);
        if (startRoom < 0 || startRoom >= level->GetRoomCount()) {
            std::fprintf(stderr, "ClassicH did not provide a valid missile starting room\n");
            delete missile;
            return 1;
        }
        missile->mPosition = level->GetStartingPos(0);
        missile->mOrientation = 0;
        MR_PhysicalCollision structureCollision;
        structureCollision.mWeight = MR_PhysicalCollision::eInfiniteWeight;
        missile->ApplyEffect(&structureCollision, 0, 0, TRUE, MR_PI, 0, 0, level);
        if (missile->mOrientation != MR_PI) {
            std::fprintf(stderr, "Missile did not bounce from a structural collision\n");
            delete missile;
            return 1;
        }
        missile->PlayExternalSounds(0, 0);
        missile->mOrientation = level->GetStartingOrientation(0);
        const int missileRoom = missile->Simulate(200, level, startRoom);
        if (missileRoom < 0 || missileRoom >= level->GetRoomCount()) {
            std::fprintf(stderr, "Missile collision simulation returned an invalid room\n");
            delete missile;
            return 1;
        }
        MR_PhysicalCollision actorCollision;
        actorCollision.mWeight = 100;
        missile->ApplyEffect(&actorCollision, 0, 0, TRUE, 0, 0, 0, level);
        if (missile->Simulate(0, level, missileRoom) != MR_Level::eMustBeDeleted) {
            std::fprintf(stderr, "Missile did not expire after hitting an actor\n");
            delete missile;
            return 1;
        }
    }
    delete missile;

    const MR_ObjectFromFactoryId mineId = {1, 151};
    MR_FreeElement* mine = dynamic_cast<MR_FreeElement*>(MR_DllObjectFactory::CreateObject(mineId));
    if (mine == nullptr) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create a mine\n");
        return 1;
    }
    mine->mPosition = MR_3DCoordinate(5000, 0, 0);
    mine->mOrientation = 0;
    viewport.Clear(0);
    viewport.ClearZ();
    mine->Render(&viewport, 0);
    delete mine;

    int minePixels = 0;
    for (int index = 0; index < pixelCount; ++index) {
        if (buffer.GetBuffer()[index] != 0) {
            ++minePixels;
        }
    }
    if (minePixels == 0) {
        std::fprintf(stderr, "Dynamic ObjFac1 mine did not render\n");
        return 1;
    }

    const MR_UInt16 surfaceClassIds[] = {50, 52, 53, 54, 55, 56, 57, 70, 71, 72, 73};
    for (const MR_UInt16 classId : surfaceClassIds) {
        const MR_ObjectFromFactoryId surfaceId = {1, classId};
        object = MR_DllObjectFactory::CreateObject(surfaceId);
        if (object == nullptr || !(object->GetTypeId() == surfaceId)) {
            std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create surface class %u\n", classId);
            delete object;
            return 1;
        }
        delete object;
    }

    const MR_UInt16 spriteClassIds[] = {1000, 1100, 1101, 1102, 1103};
    for (const MR_UInt16 classId : spriteClassIds) {
        const MR_ObjectFromFactoryId spriteId = {1, classId};
        MR_SpriteHandle* handle = static_cast<MR_SpriteHandle*>(MR_DllObjectFactory::CreateObject(spriteId));
        if (handle == nullptr || handle->GetSprite() == nullptr) {
            std::fprintf(stderr, "Dynamic ObjFac1 plugin did not create HUD sprite class %u\n", classId);
            delete handle;
            return 1;
        }
        delete handle;
    }

    if (MR_DllObjectFactory::GetObjectTypeCount(1) != 30 ||
        CString(MR_DllObjectFactory::GetObjectFamily(id)).IsEmpty()) {
        std::fprintf(stderr, "Dynamic ObjFac1 plugin did not expose factory metadata\n");
        return 1;
    }
    MR_DllObjectFactory::Clean(FALSE);

    std::puts("Dynamic ObjFac1 factory smoke test passed");
    return 0;
}
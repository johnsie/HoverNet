#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "StdAfx.h"

#include <algorithm>
#include "../Model/MazeElement.h"
#include "../Model/Level.h"
#include "../Model/ConcreteShape.h"
#include "../Model/FreeElementMovingHelper.h"
#include "../Model/PhysicalCollision.h"
#include "../Model/RaceEffects.h"
#include "../Model/ShapeCollisions.h"
#include "../MainCharacter/MainCharacterRenderer.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../ObjFacTools/ResSound.h"
#include "../ObjFacTools/SpriteHandle.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/SoundServer.h"
#include "DoubleSpeedSource.h"
#include "FinishLine.h"
#include "FuelSource.h"
#include "ObjFac1Res.h"
#include "SimElements.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

class MR_ResActorFriend
{
public:
    static void Draw(const MR_ResActor* actor, MR_3DViewPort* destination,
                     const MR_PositionMatrix& matrix, int sequence, int frame,
                     const MR_Bitmap* cockpitBitmap)
    {
        if (actor == nullptr || destination == nullptr || actor->mSequenceList == nullptr ||
            sequence < 0 || sequence >= actor->mNbSequence) {
            return;
        }

        // Manta sequence 1 is an animated exhaust overlay. Draw its complete
        // static hull first so acceleration cannot replace the craft with only
        // the flame components on Linux.
        if (actor->GetResourceId() == MR_MANTA_CRAFT && sequence == 1) {
            Draw(actor, destination, matrix, 0, 0, cockpitBitmap);
        }

        const int frameCount = actor->mSequenceList[sequence].mNbFrame;
        if (frameCount <= 0) {
            return;
        }
        frame = ((frame % frameCount) + frameCount) % frameCount;

        const MR_ResActor::Frame& actorFrame = actor->mSequenceList[sequence].mFrameList[frame];
        for (int index = 0; index < actorFrame.mNbComponent; ++index) {
            const MR_ResActor::Patch* patch = static_cast<const MR_ResActor::Patch*>(actorFrame.mComponentList[index]);
            if (patch == nullptr || patch->mBitmap == nullptr) {
                continue;
            }

            const int bitmapId = patch->mBitmap->GetResourceId();
            const MR_Bitmap* bitmap = patch->mBitmap;
            if ((bitmapId == MR_CAR_COCKPIT || bitmapId == MR_CAR2_COCKPIT || bitmapId == MR_ECAR_COCKPIT) &&
                cockpitBitmap != nullptr) {
                bitmap = cockpitBitmap;
            }
            destination->RenderPatch(*patch, matrix, bitmap);
        }
    }
};

namespace
{
std::unique_ptr<MR_ResourceLib> resourceLib;

MR_ShortSound* ShortSound(int resourceId)
{
    const MR_ResShortSound* sound = resourceLib ? resourceLib->GetShortSound(resourceId) : nullptr;
    return sound ? sound->GetSound() : nullptr;
}

MR_ContinuousSound* ContinuousSound(int resourceId)
{
    const MR_ResContinuousSound* sound = resourceLib ? resourceLib->GetContinuousSound(resourceId) : nullptr;
    return sound ? sound->GetSound() : nullptr;
}

class HeadlessBitmapSurface : public MR_SimSurface
{
public:
    HeadlessBitmapSurface(const MR_ObjectFromFactoryId& id, MR_ResBitmap* bitmap,
                          MR_ResBitmap* alternateBitmap = nullptr, int rotationSpeed = 0,
                          int rotationLength = 1, int maximumHeight = 0)
        : MR_SimSurface(id), bitmap(bitmap), alternateBitmap(alternateBitmap ? alternateBitmap : bitmap),
          rotationSpeed(rotationSpeed), rotationLength(rotationLength), maximumHeight(maximumHeight) {}

    // The collision behaviour (every wall and floor bounces craft) is MR_SimSurface's; this class only draws.
    void RenderWallSurface(MR_3DViewPort* destination, const MR_3DCoordinate& upperLeft,
                           const MR_3DCoordinate& lowerRight, MR_Int32 length,
                           MR_SimulationTime time) override
    {
        if (destination == nullptr || bitmap == nullptr) {
            return;
        }

        if (maximumHeight > 0) {
            const int height = upperLeft.mZ - lowerRight.mZ;
            if (height > 0) {
                const int divisor = 1 + (height - 1) / maximumHeight;
                bitmap->SetWidthHeight(height / divisor, height / divisor);
            }
        }

        if (rotationSpeed != 0 && rotationLength > 0) {
            int startPosition = (time + 40000) / rotationSpeed;
            startPosition = startPosition < 0
                ? rotationLength - 1 - ((-startPosition) % rotationLength)
                : startPosition % rotationLength;
            destination->RenderAlternateWallSurface(upperLeft, lowerRight, length, bitmap,
                                                    alternateBitmap, rotationLength, startPosition);
        }
        else {
            destination->RenderWallSurface(upperLeft, lowerRight, length, bitmap);
        }
    }

    void RenderHorizontalSurface(MR_3DViewPort* destination, int vertexCount,
                                 const MR_2DCoordinate* vertices, MR_Int32 level, BOOL top,
                                 MR_SimulationTime) override
    {
        if (destination != nullptr && bitmap != nullptr) {
            destination->RenderHorizontalSurface(vertexCount, vertices, level, top, bitmap);
        }
    }

private:
    MR_ResBitmap* bitmap;
    MR_ResBitmap* alternateBitmap;
    int rotationSpeed;
    int rotationLength;
    int maximumHeight;
};

class HeadlessHoverRenderer : public MR_MainCharacterRenderer
{
public:
    explicit HeadlessHoverRenderer(const MR_ObjectFromFactoryId& id)
        : MR_MainCharacterRenderer(id), frame(0) {}

    void Render(MR_3DViewPort* destination, const MR_3DCoordinate& position,
                MR_Angle orientation, BOOL motorOn, int hoverId, int actorId) override
    {
        if (destination == nullptr || !resourceLib) {
            return;
        }

        const MR_ResActor* actor = resourceLib->GetActor(actorId);
        if (actor == nullptr) {
            actor = resourceLib->GetActor(MR_ELECTRO_CAR);
        }
        if (actor == nullptr || actor->GetSequenceCount() == 0) {
            return;
        }

        const int sequence = motorOn && actor->GetSequenceCount() > 1 ? 1 : 0;
        const int frameCount = actor->GetFrameCount(sequence);
        if (frameCount == 0) {
            return;
        }

        MR_PositionMatrix matrix;
        if (!destination->ComputePositionMatrix(matrix, position, orientation, 10000000)) {
            return;
        }
        frame = motorOn ? (frame + 1) % frameCount : 0;
        const int cockpitId = (actorId == MR_HITECH_CAR ? MR_CAR_COCKPIT21 : MR_CAR_COCKPIT1) +
                      (hoverId % 10 + 10) % 10;
        MR_ResActorFriend::Draw(actor, destination, matrix, sequence, frame,
                                resourceLib->GetBitmap(cockpitId));
    }

    MR_ShortSound* GetLineCrossingSound() override { return ShortSound(MR_SND_LINE_CROSSING); }
    MR_ShortSound* GetStartSound() override { return ShortSound(MR_SND_START); }
    MR_ShortSound* GetFinishSound() override { return ShortSound(MR_SND_FINISH); }
    MR_ShortSound* GetBumpSound() override { return ShortSound(MR_SND_BUMP); }
    MR_ShortSound* GetJumpSound() override { return ShortSound(MR_SND_JUMP); }
    MR_ShortSound* GetFireSound() override { return ShortSound(MR_SND_FIRE); }
    MR_ShortSound* GetMisJumpSound() override { return ShortSound(MR_SND_MIS_JUMP); }
    MR_ShortSound* GetMisFireSound() override { return ShortSound(MR_SND_MIS_FIRE); }
    MR_ShortSound* GetOutOfCtrlSound() override { return ShortSound(MR_SND_OUT_OF_CTRL); }
    MR_ContinuousSound* GetMotorSound() override { return ContinuousSound(MR_SND_MOTOR); }
    MR_ContinuousSound* GetFrictionSound() override { return ContinuousSound(MR_SND_FRICTION); }

private:
    int frame;
};

// Headless port of MR_TestElement (TestElement.cpp): the animated, pushable
// actor (the "electro car" and "demo fighter" models) that classic community
// tracks scatter around as moving obstacles. It falls under gravity, steps up
// small ledges, is shoved by craft and missiles, and loops its animation.
// The elements below are the game client's presentation of the simulation classes in SimElements.h: they add drawing,
// animation frames and sound, and nothing that affects the race.

class HeadlessTestElement : public MR_SimTestElement
{
public:
    HeadlessTestElement(const MR_ObjectFromFactoryId& id, int actorResource)
        : MR_SimTestElement(id)
    {
        actor = resourceLib ? resourceLib->GetActor(actorResource) : nullptr;
    }

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        if (destination == nullptr || actor == nullptr || actor->GetSequenceCount() == 0) {
            return;
        }
        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 1000)) {
            actor->Draw(destination, matrix, currentSequence, currentFrame);
        }
    }

    int Simulate(MR_SimulationTime duration, MR_Level* level, int room) override
    {
        elapsedFrameTime += duration;
        const int frameIncrement = elapsedFrameTime / 75;
        if (frameIncrement > 0 && actor != nullptr && actor->GetSequenceCount() > 0) {
            elapsedFrameTime %= 75;
            currentFrame += frameIncrement;
            if (currentFrame >= actor->GetFrameCount(currentSequence)) {
                currentFrame = 0;
                if (++currentSequence >= actor->GetSequenceCount()) {
                    currentSequence = 0;
                }
            }
        }
        return MR_SimTestElement::Simulate(duration, level, room);
    }

private:
    const MR_ResActor* actor = nullptr;
    int currentSequence = 0;
    int currentFrame = 0;
    MR_SimulationTime elapsedFrameTime = 0;
};

class HeadlessPowerUp : public MR_SimPowerUp
{
public:
    explicit HeadlessPowerUp(const MR_ObjectFromFactoryId& id) : MR_SimPowerUp(id) {}

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_PWRUP) : nullptr;
        if (destination == nullptr || actor == nullptr || actor->GetSequenceCount() == 0 ||
            actor->GetFrameCount(0) == 0) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 1000)) {
            actor->Draw(destination, matrix, 0, 0);
        }
    }
};

class HeadlessBumperGate : public MR_SimBumperGate
{
public:
    explicit HeadlessBumperGate(const MR_ObjectFromFactoryId& id) : MR_SimBumperGate(id) {}

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_BUMPERGATE) : nullptr;
        if (destination == nullptr || actor == nullptr || actor->GetSequenceCount() == 0 ||
            actor->GetFrameCount(0) == 0) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 2500)) {
            actor->Draw(destination, matrix, 0, CurrentFrame());
        }
    }
};

class HeadlessMissile : public MR_SimMissile
{
public:
    explicit HeadlessMissile(const MR_ObjectFromFactoryId& id) : MR_SimMissile(id) {}

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_MISSILE) : nullptr;
        if (destination == nullptr || actor == nullptr) {
            return;
        }

        // Ignition animation, then the flying loop, then the burn-out after the missile stops.
        const MR_SimulationTime lived = Lived();
        int sequence = 0;
        int frame = 0;
        if (lived > 7500 && actor->GetSequenceCount() > 2) {
            sequence = 2;
            const int frameCount = actor->GetFrameCount(sequence);
            frame = frameCount > 0 ? std::min<int>(frameCount - 1, frameCount * (lived - 7500) / 1200) : 0;
        }
        else if (lived < 525) {
            const int frameCount = actor->GetFrameCount(sequence);
            frame = frameCount > 0 ? std::min<int>(frameCount - 1, frameCount * lived / 525) : 0;
        }
        else if (actor->GetSequenceCount() > 1) {
            sequence = 1;
            const int frameCount = actor->GetFrameCount(sequence);
            frame = frameCount > 0 ? (lived / 256) % frameCount : 0;
        }
        if (sequence >= actor->GetSequenceCount() || frame >= actor->GetFrameCount(sequence)) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 300)) {
            actor->Draw(destination, matrix, sequence, frame);
        }
    }

    void PlayExternalSounds(int decibels, int pan) override
    {
        if (TakeBounceEvent()) {
            MR_SoundServer::Play(ShortSound(MR_SND_MISSILE_BOUNCE), decibels, 1.0, pan);
        }
        MR_SoundServer::Play(ContinuousSound(MR_SND_MISSILE_MOTOR), 1, decibels, 1.0, pan);
    }
};

class HeadlessMine : public MR_SimMine
{
public:
    explicit HeadlessMine(const MR_ObjectFromFactoryId& id) : MR_SimMine(id) {}

    void Render(MR_3DViewPort* destination, MR_SimulationTime time) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_MINE) : nullptr;
        if (destination == nullptr || actor == nullptr || actor->GetSequenceCount() == 0 ||
            actor->GetFrameCount(0) == 0) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 400)) {
            actor->Draw(destination, matrix, 0, (time >> 9) % actor->GetFrameCount(0));
        }
    }
};


MR_ResBitmap* BitmapForClass(MR_UInt16 classId)
{
    if (!resourceLib) {
        return nullptr;
    }

    switch (classId) {
    case 50: return nullptr;
    case 51: return resourceLib->GetBitmap(MR_STD_FLOOR);
    case 52: return resourceLib->GetBitmap(MR_STD_RIGHT_WALL);
    case 53: return resourceLib->GetBitmap(MR_STD_LEFT_WALL);
    case 54: return resourceLib->GetBitmap(MR_RED_RIGHT_WALL_OFF);
    case 55: return resourceLib->GetBitmap(MR_RED_LEFT_WALL_OFF);
    case 56: return resourceLib->GetBitmap(MR_GREEN_RIGHT_WALL_OFF);
    case 57: return resourceLib->GetBitmap(MR_GREEN_LEFT_WALL_OFF);
    case 58: return resourceLib->GetBitmap(MR_STEP_WALL);
    case 59: return resourceLib->GetBitmap(MR_PASS_RIGHT_WALL);
    case 60: return resourceLib->GetBitmap(MR_PASS_LEFT_WALL);
    case 61: return resourceLib->GetBitmap(MR_DO_NOT_ENTER_WALL1);
    case 62: return resourceLib->GetBitmap(MR_DO_NOT_ENTER_WALL2);
    case 63: return resourceLib->GetBitmap(MR_BLUE_BUBBLE_FLOOR);
    case 64: return resourceLib->GetBitmap(MR_SPEED_ZONE);
    case 65: return resourceLib->GetBitmap(MR_FUEL_ZONE);
    case 66: return resourceLib->GetBitmap(MR_YELLOW_STEP);
    case 67: return resourceLib->GetBitmap(MR_CHECKER);
    case 68: return resourceLib->GetBitmap(MR_PIT_WORD);
    case 69: return resourceLib->GetBitmap(MR_FINISH_WORD);
    case 70:
    case 71: return resourceLib->GetBitmap(MR_YELLOW_NEON);
    case 72: return resourceLib->GetBitmap(MR_STD_WALL);
    case 73: return resourceLib->GetBitmap(MR_STD_WALL_TOP);
    default: return nullptr;
    }
}

MR_ObjectFromFactory* CreateSurface(const MR_ObjectFromFactoryId& id)
{
    if (!resourceLib) {
        return nullptr;
    }

    switch (id.mClassId) {
    case 52:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_STD_RIGHT_WALL), nullptr, 0, 1, 4000);
    case 53:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_STD_LEFT_WALL), nullptr, 0, 1, 4000);
    case 54:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_RED_RIGHT_WALL_OFF),
                                         resourceLib->GetBitmap(MR_RED_RIGHT_WALL), 200, 4, 4000);
    case 55:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_RED_LEFT_WALL_OFF),
                                         resourceLib->GetBitmap(MR_RED_LEFT_WALL), -200, 4, 4000);
    case 56:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_GREEN_RIGHT_WALL_OFF),
                                         resourceLib->GetBitmap(MR_GREEN_RIGHT_WALL), 200, 4, 4000);
    case 57:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_GREEN_LEFT_WALL_OFF),
                                         resourceLib->GetBitmap(MR_GREEN_LEFT_WALL), -200, 4, 4000);
    case 70:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_YELLOW_NEON),
                                         resourceLib->GetBitmap(MR_YELLOW_NEON_FLASH), 50, 20);
    case 71:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_YELLOW_NEON),
                                         resourceLib->GetBitmap(MR_YELLOW_NEON_FLASH), -50, 20);
    case 72:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_STD_WALL), nullptr, 0, 1, 4000);
    case 73:
        return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_STD_WALL_TOP), nullptr, 0, 1, 6000);
    default:
        return new HeadlessBitmapSurface(id, BitmapForClass(id.mClassId));
    }
}
}

#ifdef _WIN32
#define HOVERNET_PLUGIN_EXPORT __declspec(dllexport)
#else
#define HOVERNET_PLUGIN_EXPORT
#endif

extern "C"
{
HOVERNET_PLUGIN_EXPORT void MR_InitModule(HMODULE module)
{
    MR_InitTrigoTables();
    const char* dataDirectory = std::getenv("HOVERNET_DATA_DIR");
    std::string lDatPath;
#ifdef HOVERNET_WEB
    lDatPath = "/NetTarget/ObjFac1.dat";
#endif
    if (lDatPath.empty() && dataDirectory != nullptr && dataDirectory[0] != '\0') {
        lDatPath = std::string(dataDirectory) + "/NetTarget/ObjFac1.dat";
    }
#ifdef _WIN32
    else if (lDatPath.empty()) {
        char modulePath[MAX_PATH];
        const DWORD modulePathLength = GetModuleFileNameA(module, modulePath, MAX_PATH);
        if (modulePathLength > 0 && modulePathLength < MAX_PATH) {
            std::string moduleDirectory(modulePath, modulePathLength);
            const std::string::size_type separator = moduleDirectory.find_last_of("\\/");
            if (separator != std::string::npos) {
                moduleDirectory.resize(separator);
                const std::string installedDataPath = moduleDirectory + "/NetTarget/ObjFac1.dat";
                if (GetFileAttributesA(installedDataPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    lDatPath = installedDataPath;
                }
            }
        }
#ifdef HOVERNET_SOURCE_DIR
        if (lDatPath.empty()) lDatPath = HOVERNET_SOURCE_DIR "/NetTarget/ObjFac1.dat";
#else
        if (lDatPath.empty()) lDatPath = "NetTarget/ObjFac1.dat";
#endif
    }
#elif defined(HOVERNET_SOURCE_DIR)
    else if (lDatPath.empty()) {
        lDatPath = HOVERNET_SOURCE_DIR "/NetTarget/ObjFac1.dat";
    }
#else
    else if (lDatPath.empty()) {
        lDatPath = "../../NetTarget/ObjFac1.dat";
    }
#endif

    try {
        resourceLib.reset(new MR_ResourceLib(lDatPath.c_str()));
    }
    catch (...) {
        resourceLib.reset();
        // Without this, a missing/corrupt ObjFac1.dat leaves every track, car and
        // effect silently absent (each call site here null-checks resourceLib and
        // renders nothing) with no indication anywhere of why -- set HOVERNET_DATA_DIR
        // if the game isn't being run from its own source/install tree.
        std::fprintf(stderr,
            "MR_InitModule: failed to load object resources from '%s' -- "
            "tracks, hovercraft and effects will not render. Set HOVERNET_DATA_DIR "
            "to the HoverRace install/source directory if this path is wrong.\n",
            lDatPath.c_str());
    }
}

HOVERNET_PLUGIN_EXPORT void MR_CleanModule()
{
    resourceLib.reset();
    MR_SoundServer::Close();
}

HOVERNET_PLUGIN_EXPORT MR_UInt16 MR_GetObjectTypeCount()
{
    return 30;
}

HOVERNET_PLUGIN_EXPORT CString MR_GetObjectFamily(MR_UInt16)
{
    return "Headless surfaces";
}

HOVERNET_PLUGIN_EXPORT CString MR_GetObjectDescription(MR_UInt16 classId)
{
    if (classId >= 1 && classId <= 5) {
        return "Classic surface";
    }
    if (classId >= 50 && classId <= 73) {
        return "Headless track surface";
    }
    if (classId >= 202 && classId <= 204) {
        return "Checkpoint";
    }
    if (classId == 152) {
        return "Power-up";
    }
    if (classId == 170) {
        return "Bumper gate";
    }
    if (classId == 150) {
        return "Missile";
    }
    if (classId == 151) {
        return "Mine";
    }
    if (classId == 200) {
        return "Fuel source";
    }
    if (classId == 201) {
        return "Speed source";
    }
    if (classId >= 1000 && classId <= 1103) {
        return "HUD sprite";
    }
    return "";
}

HOVERNET_PLUGIN_EXPORT MR_ObjectFromFactory* MR_GetObject(MR_UInt16 classId)
{
    const MR_ObjectFromFactoryId id = {1, classId};
    switch (classId) {
    case 10:
        return new HeadlessTestElement(id, MR_DEMO_FIGHTER);
    case 13:
        return new HeadlessTestElement(id, MR_ELECTRO_CAR);
    case 100:
        return new HeadlessHoverRenderer(id);
    case 152:
        return new HeadlessPowerUp(id);
    case 150:
        return new HeadlessMissile(id);
    case 151:
        return new HeadlessMine(id);
    case 170:
        return new HeadlessBumperGate(id);
    case 202:
        return new MR_FinishLine(id, MR_CheckPoint::eFinishLine);
    case 200:
        return new MR_FuelSource(id);
    case 201:
        return new MR_DoubleSpeedSource(id);
    case 203:
        return new MR_FinishLine(id, MR_CheckPoint::eCheck1);
    case 204:
        return new MR_FinishLine(id, MR_CheckPoint::eCheck2);
    case 1000:
        return resourceLib ? new MR_SpriteHandle(id, resourceLib->GetSprite(MR_FONT1)) : nullptr;
    case 1100:
        return resourceLib ? new MR_SpriteHandle(id, resourceLib->GetSprite(MR_MISSILE_STAT)) : nullptr;
    case 1101:
        return resourceLib ? new MR_SpriteHandle(id, resourceLib->GetSprite(MR_HOVER_ICONS)) : nullptr;
    case 1102:
        return resourceLib ? new MR_SpriteHandle(id, resourceLib->GetSprite(MR_MINE_STAT)) : nullptr;
    case 1103:
        return resourceLib ? new MR_SpriteHandle(id, resourceLib->GetSprite(MR_PWRUP_STAT)) : nullptr;
    default:
        break;
    }

    // Classic plain-texture surfaces (MR_DefaultSurface, MR_WoodSurface and the
    // fire/brick bitmap surfaces in main.cpp's MR_GetObject). Community tracks
    // built with HoverCad use them; they carry no per-instance state.
    if (resourceLib) {
        switch (classId) {
        case 1: return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_ETALON));
        case 2: return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_WOOD1));
        case 3: return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_FIRE));
        case 4: return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_BIG_BRICK));
        case 5: return new HeadlessBitmapSurface(id, resourceLib->GetBitmap(MR_RED_BRICK));
        default: break;
        }
    }

    if (classId < 50 || classId > 73 || !resourceLib) {
        return nullptr;
    }

    return CreateSurface(id);
}
}

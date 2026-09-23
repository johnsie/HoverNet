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

#include <memory>

class MR_ResActorFriend
{
public:
    static void Draw(const MR_ResActor* actor, MR_3DViewPort* destination,
                     const MR_PositionMatrix& matrix, int sequence, int frame,
                     const MR_Bitmap* cockpitBitmap)
    {
        if (actor == nullptr || destination == nullptr || sequence < 0 ||
            sequence >= actor->mNbSequence || actor->mSequenceList == nullptr ||
            frame < 0 || frame >= actor->mSequenceList[sequence].mNbFrame) {
            return;
        }

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

class HeadlessBitmapSurface : public MR_SurfaceElement
{
public:
    HeadlessBitmapSurface(const MR_ObjectFromFactoryId& id, MR_ResBitmap* bitmap,
                          MR_ResBitmap* alternateBitmap = nullptr, int rotationSpeed = 0,
                          int rotationLength = 1, int maximumHeight = 0)
        : MR_SurfaceElement(id), bitmap(bitmap), alternateBitmap(alternateBitmap ? alternateBitmap : bitmap),
          rotationSpeed(rotationSpeed), rotationLength(rotationLength), maximumHeight(maximumHeight) {}

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

class HeadlessPowerUp : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit HeadlessPowerUp(const MR_ObjectFromFactoryId& id)
        : MR_FreeElement(id)
    {
        effect.mElementPermId = -1;
        effects.AddTail(&effect);
    }

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

protected:
    MR_Int32 ZMin() const override { return mPosition.mZ - 550; }
    MR_Int32 ZMax() const override { return mPosition.mZ + 550; }
    MR_Int32 AxisX() const override { return mPosition.mX; }
    MR_Int32 AxisY() const override { return mPosition.mY; }
    MR_Int32 RayLen() const override { return 550; }

    const MR_ContactEffectList* GetEffectList() override { return &effects; }
    const MR_ShapeInterface* GetReceivingContactEffectShape() override { return this; }

    int Simulate(MR_SimulationTime duration, MR_Level*, int room) override
    {
        if (duration != 0) {
            mOrientation = MR_NORMALIZE_ANGLE(mOrientation + duration);
        }
        return room;
    }

    BOOL AssignPermNumber(int number) override
    {
        effect.mElementPermId = number;
        return TRUE;
    }

private:
    MR_PowerUpEffect effect;
    MR_ContactEffectList effects;
};

class HeadlessBumperGate : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit HeadlessBumperGate(const MR_ObjectFromFactoryId& id)
        : MR_FreeElement(id), timeSinceLastCollision(1000000), lastState(0), currentFrame(0)
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_BUMPERGATE) : nullptr;
        if (actor != nullptr && actor->GetSequenceCount() > 0) {
            lastState = actor->GetFrameCount(0) - 1;
        }
        currentFrame = lastState;
        effects.AddTail(&collisionEffect);
    }

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_BUMPERGATE) : nullptr;
        if (destination == nullptr || actor == nullptr || actor->GetSequenceCount() == 0 ||
            actor->GetFrameCount(0) == 0) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 2500)) {
            actor->Draw(destination, matrix, 0, currentFrame);
        }
    }

protected:
    MR_Int32 ZMin() const override { return mPosition.mZ + 2; }
    MR_Int32 ZMax() const override
    {
        return mPosition.mZ + 1500 + currentFrame * (3000 - 1500) / std::max(1, lastState);
    }
    MR_Int32 AxisX() const override { return mPosition.mX; }
    MR_Int32 AxisY() const override { return mPosition.mY; }
    MR_Int32 RayLen() const override
    {
        return 200 + currentFrame * (2500 - 200) / std::max(1, lastState);
    }

    const MR_ContactEffectList* GetEffectList() override
    {
        collisionEffect.mWeight = MR_PhysicalCollision::eInfiniteWeight;
        collisionEffect.mXSpeed = 0;
        collisionEffect.mYSpeed = 0;
        collisionEffect.mZSpeed = 0;
        return &effects;
    }

    const MR_ShapeInterface* GetGivingContactEffectShape() override { return nullptr; }
    const MR_ShapeInterface* GetReceivingContactEffectShape() override { return this; }

    int Simulate(MR_SimulationTime duration, MR_Level*, int room) override
    {
        if (duration >= 0) {
            timeSinceLastCollision += duration;
            if (timeSinceLastCollision < 1500) {
                currentFrame = (1500 - timeSinceLastCollision) * lastState / 1500;
            }
            else if (timeSinceLastCollision < 9000) {
                currentFrame = 0;
            }
            else if (timeSinceLastCollision < 13000) {
                currentFrame = (timeSinceLastCollision - 9000) * lastState / 4000;
            }
            else {
                currentFrame = lastState;
            }
        }
        return room;
    }

    void ApplyEffect(const MR_ContactEffect* effect, MR_SimulationTime, MR_SimulationTime,
                     BOOL, MR_Angle, MR_Int32, MR_Int32, MR_Level*) override
    {
        if (dynamic_cast<const MR_PhysicalCollision*>(effect) == nullptr) {
            return;
        }
        timeSinceLastCollision = currentFrame >= lastState || lastState == 0
            ? 0
            : 1500 - 1500 * currentFrame / lastState;
    }

private:
    MR_SimulationTime timeSinceLastCollision;
    int lastState;
    int currentFrame;
    MR_PhysicalCollision collisionEffect;
    MR_ContactEffectList effects;
};

class HeadlessMissile : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit HeadlessMissile(const MR_ObjectFromFactoryId& id)
        : MR_FreeElement(id), hoverId(-1), lived(0), currentSequence(0), currentFrame(0), bounceSoundEvent(FALSE)
    {
        effects.AddTail(&collisionEffect);
        effects.AddTail(&lostOfControlEffect);
        lostOfControlEffect.mType = MR_LostOfControl::eMissile;
        lostOfControlEffect.mElementId = -1;
        lostOfControlEffect.mHoverId = hoverId;
    }

    void Render(MR_3DViewPort* destination, MR_SimulationTime) override
    {
        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_MISSILE) : nullptr;
        if (destination == nullptr || actor == nullptr || currentSequence >= actor->GetSequenceCount() ||
            currentFrame >= actor->GetFrameCount(currentSequence)) {
            return;
        }

        MR_PositionMatrix matrix;
        if (destination->ComputePositionMatrix(matrix, mPosition, mOrientation, 300)) {
            actor->Draw(destination, matrix, currentSequence, currentFrame);
        }
    }

protected:
    MR_Int32 ZMin() const override { return mPosition.mZ - 300; }
    MR_Int32 ZMax() const override { return mPosition.mZ + 300; }
    MR_Int32 AxisX() const override { return mPosition.mX; }
    MR_Int32 AxisY() const override { return mPosition.mY; }
    MR_Int32 RayLen() const override { return 300; }

    void SetOwnerId(int owner) override
    {
        hoverId = owner;
        lostOfControlEffect.mHoverId = owner;
    }

    const MR_ContactEffectList* GetEffectList() override
    {
        if (lived <= 175) {
            return nullptr;
        }
        collisionEffect.mWeight = 100;
        collisionEffect.mXSpeed = 21 * 2222 * MR_Cos[mOrientation] / (1000 * MR_TRIGO_FRACT) * 256;
        collisionEffect.mYSpeed = 21 * 2222 * MR_Sin[mOrientation] / (1000 * MR_TRIGO_FRACT) * 256;
        collisionEffect.mZSpeed = 0;
        return &effects;
    }

    const MR_ShapeInterface* GetGivingContactEffectShape() override { return this; }
    const MR_ShapeInterface* GetReceivingContactEffectShape() override { return this; }

    void ApplyEffect(const MR_ContactEffect* effect, MR_SimulationTime, MR_SimulationTime,
                     BOOL validDirection, MR_Angle horizontalDirection, MR_Int32, MR_Int32,
                     MR_Level*) override
    {
        const MR_PhysicalCollision* collision = dynamic_cast<const MR_PhysicalCollision*>(effect);
        if (collision == nullptr || !validDirection) {
            return;
        }

        if (collision->mWeight < MR_PhysicalCollision::eInfiniteWeight) {
            if (lived >= 175) {
                lived = 8700;
            }
            return;
        }

        const MR_Angle difference = MR_NORMALIZE_ANGLE(horizontalDirection - mOrientation + MR_PI);
        if (difference < MR_PI / 2 || difference > MR_PI + MR_PI / 2) {
            mOrientation = MR_NORMALIZE_ANGLE(horizontalDirection + difference);
            bounceSoundEvent = TRUE;
        }
    }

    void PlayExternalSounds(int decibels, int pan) override
    {
        if (bounceSoundEvent) {
            MR_SoundServer::Play(ShortSound(MR_SND_MISSILE_BOUNCE), decibels, 1.0, pan);
            bounceSoundEvent = FALSE;
        }
        MR_SoundServer::Play(ContinuousSound(MR_SND_MISSILE_MOTOR), 1, decibels, 1.0, pan);
    }

    int Simulate(MR_SimulationTime duration, MR_Level* level, int room) override
    {
        MR_SimulationTime remainingDuration = duration;
        while (remainingDuration > 0) {
            const MR_SimulationTime timeSlice = std::min<MR_SimulationTime>(remainingDuration, 5);
            lived += timeSlice;
            if (lived < 7500 && level != nullptr) {
                const MR_Int32 speed = 21 * 2222 / 1000;
                const MR_Int32 xTranslation = timeSlice * speed * MR_Cos[mOrientation] / MR_TRIGO_FRACT;
                const MR_Int32 yTranslation = timeSlice * speed * MR_Sin[mOrientation] / MR_TRIGO_FRACT;
                MR_Cylinder shape;
                shape.mRayLen = 1;
                shape.mZMin = ZMin();
                shape.mZMax = ZMax();
                shape.mAxis.mX = mPosition.mX + xTranslation;
                shape.mAxis.mY = mPosition.mY + yTranslation;

                MR_ObstacleCollisionReport report;
                MR_SimulationTime attemptedDuration = timeSlice;
                while (true) {
                    report.GetContactWithObstacles(level, &shape, room, this);
                    if (report.IsInMaze() && !report.HaveContact()) {
                        mPosition.mX = shape.mAxis.mX;
                        mPosition.mY = shape.mAxis.mY;
                        room = report.Room();
                        if (attemptedDuration == timeSlice) {
                            break;
                        }
                        attemptedDuration /= 2;
                        shape.mAxis.mX += attemptedDuration * xTranslation / timeSlice;
                        shape.mAxis.mY += attemptedDuration * yTranslation / timeSlice;
                    }
                    else {
                        if (attemptedDuration < 6) {
                            break;
                        }
                        attemptedDuration /= 2;
                        shape.mAxis.mX -= attemptedDuration * xTranslation / timeSlice;
                        shape.mAxis.mY -= attemptedDuration * yTranslation / timeSlice;
                    }
                }
            }
            remainingDuration -= timeSlice;
        }

        const MR_ResActor* actor = resourceLib ? resourceLib->GetActor(MR_MISSILE) : nullptr;
        if (actor != nullptr) {
            if (lived > 7500 && actor->GetSequenceCount() > 2) {
                currentSequence = 2;
                const int frameCount = actor->GetFrameCount(currentSequence);
                currentFrame = frameCount > 0 ? std::min(frameCount - 1, frameCount * (lived - 7500) / 1200) : 0;
            }
            else if (lived < 525) {
                currentSequence = 0;
                const int frameCount = actor->GetFrameCount(currentSequence);
                currentFrame = frameCount > 0 ? std::min(frameCount - 1, frameCount * lived / 525) : 0;
            }
            else if (actor->GetSequenceCount() > 1) {
                currentSequence = 1;
                const int frameCount = actor->GetFrameCount(currentSequence);
                currentFrame = frameCount > 0 ? (lived / 256) % frameCount : 0;
            }
        }
        return lived >= 8700 ? MR_Level::eMustBeDeleted : room;
    }

private:
    int hoverId;
    MR_SimulationTime lived;
    int currentSequence;
    int currentFrame;
    BOOL bounceSoundEvent;
    MR_PhysicalCollision collisionEffect;
    MR_LostOfControl lostOfControlEffect;
    MR_ContactEffectList effects;
};

class HeadlessMine : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit HeadlessMine(const MR_ObjectFromFactoryId& id)
        : MR_FreeElement(id), onGround(FALSE)
    {
        effects.AddTail(&lostOfControlEffect);
        lostOfControlEffect.mType = MR_LostOfControl::eMine;
        lostOfControlEffect.mElementId = -1;
        lostOfControlEffect.mHoverId = -1;
        mOrientation = 0;
    }

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

protected:
    MR_Int32 ZMin() const override { return mPosition.mZ; }
    MR_Int32 ZMax() const override { return mPosition.mZ + 140; }
    MR_Int32 AxisX() const override { return mPosition.mX; }
    MR_Int32 AxisY() const override { return mPosition.mY; }
    MR_Int32 RayLen() const override { return 400; }

    const MR_ContactEffectList* GetEffectList() override { return onGround ? &effects : nullptr; }
    const MR_ShapeInterface* GetGivingContactEffectShape() override { return nullptr; }
    const MR_ShapeInterface* GetReceivingContactEffectShape() override { return this; }

    BOOL AssignPermNumber(int number) override
    {
        lostOfControlEffect.mElementId = number;
        return TRUE;
    }

    int Simulate(MR_SimulationTime duration, MR_Level* level, int room) override
    {
        if (room == -1) {
            onGround = FALSE;
        }
        else if (!onGround && duration > 0 && level != nullptr) {
            mPosition.mZ -= duration * 0.6;
            MR_ObstacleCollisionReport report;
            report.GetContactWithObstacles(level, this, room, this);
            if (!report.IsInMaze()) {
                onGround = TRUE;
            }
            else if (report.HaveContact()) {
                mPosition.mZ += report.StepHeight();
                onGround = TRUE;
            }
        }
        return room;
    }

private:
    BOOL onGround;
    MR_LostOfControl lostOfControlEffect;
    MR_ContactEffectList effects;
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

extern "C"
{
void MR_InitModule(HMODULE)
{
    try {
    MR_InitTrigoTables();
#ifdef HOVERNET_SOURCE_DIR
    resourceLib.reset(new MR_ResourceLib(HOVERNET_SOURCE_DIR "/NetTarget/ObjFac1.dat"));
#else
    resourceLib.reset(new MR_ResourceLib("../../NetTarget/ObjFac1.dat"));
#endif
    }
    catch (...) {
        resourceLib.reset();
    }
}

void MR_CleanModule()
{
    resourceLib.reset();
    MR_SoundServer::Close();
}

MR_UInt16 MR_GetObjectTypeCount()
{
    return 30;
}

CString MR_GetObjectFamily(MR_UInt16)
{
    return "Headless surfaces";
}

CString MR_GetObjectDescription(MR_UInt16 classId)
{
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

MR_ObjectFromFactory* MR_GetObject(MR_UInt16 classId)
{
    const MR_ObjectFromFactoryId id = {1, classId};
    switch (classId) {
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

    if (classId < 50 || classId > 73 || !resourceLib) {
        return nullptr;
    }

    return CreateSurface(id);
}
}
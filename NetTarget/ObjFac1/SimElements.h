// SimElements.h
//
// The simulation half of the track elements that take part in a race: surfaces, power-ups, bumper gates, missiles
// and mines (fuel, speed and finish-line elements are already pure simulation, see FuelSource.h and friends).
// Nothing here knows about bitmaps, sprites, actors or sound; the game client derives presentation classes from
// these (LinuxPlugin.cpp) and a headless program registers them directly with MR_RegisterSimElements(), so it
// needs neither the ObjFac1 plug-in nor its resource file. See docs/adr/0001-simulation-core.md.
#ifndef SIM_ELEMENTS_H
#define SIM_ELEMENTS_H

#include "../Model/ConcreteShape.h"
#include "../Model/MazeElement.h"
#include "../Model/PhysicalCollision.h"
#include "../Model/RaceEffects.h"
#include "../Model/StateHash.h"

// Frames of the bumper gate's animation. The gate's collision shape grows with its current frame, so this is
// simulation data; HoverNetBumperGateFrames (ctest) checks it against the resource file.
const int kBumperGateFrames = 10;

// A wall, floor or ceiling: carries an infinite-weight collision effect so craft bounce off it.
class MR_SimSurface : public MR_SurfaceElement
{
public:
    explicit MR_SimSurface( const MR_ObjectFromFactoryId& pId );
    const MR_ContactEffectList* GetEffectList() override;
};

class MR_SimPowerUp : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit MR_SimPowerUp( const MR_ObjectFromFactoryId& pId );

protected:
    MR_Int32 ZMin() const override;
    MR_Int32 ZMax() const override;
    MR_Int32 AxisX() const override;
    MR_Int32 AxisY() const override;
    MR_Int32 RayLen() const override;
    const MR_ContactEffectList* GetEffectList() override;
    const MR_ShapeInterface* GetReceivingContactEffectShape() override;
    int Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom ) override;
    BOOL AssignPermNumber( int pNumber ) override;
    void VisitLogicState( MR_StateVisitor& pVisitor ) override;

private:
    MR_PowerUpEffect mEffect;
    MR_ContactEffectList mEffects;
};

class MR_SimBumperGate : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit MR_SimBumperGate( const MR_ObjectFromFactoryId& pId );
    int CurrentFrame() const { return mCurrentFrame; }

protected:
    MR_Int32 ZMin() const override;
    MR_Int32 ZMax() const override;
    MR_Int32 AxisX() const override;
    MR_Int32 AxisY() const override;
    MR_Int32 RayLen() const override;
    const MR_ContactEffectList* GetEffectList() override;
    const MR_ShapeInterface* GetGivingContactEffectShape() override;
    const MR_ShapeInterface* GetReceivingContactEffectShape() override;
    int Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom ) override;
    void ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime pTime, MR_SimulationTime pDuration, BOOL pValidDirection,
                      MR_Angle pHorizontalDirection, MR_Int32 pZMin, MR_Int32 pZMax, MR_Level* pLevel ) override;
    void VisitLogicState( MR_StateVisitor& pVisitor ) override;

private:
    MR_SimulationTime mTimeSinceLastCollision;
    int mLastState;
    int mCurrentFrame;
    MR_PhysicalCollision mCollisionEffect;
    MR_ContactEffectList mEffects;
};

class MR_SimMissile : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit MR_SimMissile( const MR_ObjectFromFactoryId& pId );
    MR_SimulationTime Lived() const { return mLived; }
    // True once after the missile bounced off a wall; the presentation uses it to play the bounce sound.
    BOOL TakeBounceEvent() { const BOOL lEvent = mBounceSoundEvent; mBounceSoundEvent = FALSE; return lEvent; }

protected:
    MR_Int32 ZMin() const override;
    MR_Int32 ZMax() const override;
    MR_Int32 AxisX() const override;
    MR_Int32 AxisY() const override;
    MR_Int32 RayLen() const override;
    void SetOwnerId( int pOwner ) override;
    const MR_ContactEffectList* GetEffectList() override;
    const MR_ShapeInterface* GetGivingContactEffectShape() override;
    const MR_ShapeInterface* GetReceivingContactEffectShape() override;
    void ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime pTime, MR_SimulationTime pDuration, BOOL pValidDirection,
                      MR_Angle pHorizontalDirection, MR_Int32 pZMin, MR_Int32 pZMax, MR_Level* pLevel ) override;
    int Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom ) override;
    void VisitLogicState( MR_StateVisitor& pVisitor ) override;

private:
    int InternalSimulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom );

    int mHoverId;
    MR_SimulationTime mLived;
    double mXSpeed;
    double mYSpeed;
    BOOL mBounceSoundEvent;
    MR_PhysicalCollision mCollisionEffect;
    MR_LostOfControl mLostOfControlEffect;
    MR_ContactEffectList mEffects;
};

class MR_SimMine : public MR_FreeElement, protected MR_CylinderShape
{
public:
    explicit MR_SimMine( const MR_ObjectFromFactoryId& pId );

protected:
    MR_Int32 ZMin() const override;
    MR_Int32 ZMax() const override;
    MR_Int32 AxisX() const override;
    MR_Int32 AxisY() const override;
    MR_Int32 RayLen() const override;
    const MR_ContactEffectList* GetEffectList() override;
    const MR_ShapeInterface* GetGivingContactEffectShape() override;
    const MR_ShapeInterface* GetReceivingContactEffectShape() override;
    BOOL AssignPermNumber( int pNumber ) override;
    int Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom ) override;
    void VisitLogicState( MR_StateVisitor& pVisitor ) override;

private:
    BOOL mOnGround;
    MR_LostOfControl mLostOfControlEffect;
    MR_ContactEffectList mEffects;
};

// The physical "test element": a free-standing body that slides, falls and is knocked about by craft (classes 10 and
// 13; community tracks use them as balls and pucks).
class MR_SimTestElement : public MR_FreeElement
{
protected:
    class Cylinder : public MR_CylinderShape
    {
    public:
        MR_Int32 mRay = 0;
        MR_3DCoordinate mPosition;

        MR_Int32 ZMin() const override { return mPosition.mZ; }
        MR_Int32 ZMax() const override { return mPosition.mZ + 1800; }
        MR_Int32 AxisX() const override { return mPosition.mX; }
        MR_Int32 AxisY() const override { return mPosition.mY; }
        MR_Int32 RayLen() const override { return mRay; }
    };

public:
    explicit MR_SimTestElement( const MR_ObjectFromFactoryId& pId );

protected:
    int Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom ) override;
    const MR_ShapeInterface* GetObstacleShape() override;
    void ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime pTime, MR_SimulationTime pDuration, BOOL pValidDirection,
                      MR_Angle pHorizontalDirection, MR_Int32 pZMin, MR_Int32 pZMax, MR_Level* pLevel ) override;
    const MR_ContactEffectList* GetEffectList() override;
    const MR_ShapeInterface* GetReceivingContactEffectShape() override;
    const MR_ShapeInterface* GetGivingContactEffectShape() override;
    void VisitLogicState( MR_StateVisitor& pVisitor ) override;

private:
    int mXSpeed;
    int mYSpeed;
    Cylinder mCollisionShape;
    Cylinder mContactShape;
    MR_PhysicalCollision mContactEffect;
    MR_ContactEffectList mContactEffectList;
};

// Creates the simulation object for a class id of the standard element set (object factory 1), or NULL if the id has
// no simulation part (hover renderer, sprites, demo elements).
MR_ObjectFromFactory* MR_CreateSimElement( MR_UInt16 pClassId );

// Registers MR_CreateSimElement as object factory 1 for programs without the ObjFac1 plug-in.
void MR_RegisterSimElements();

#endif

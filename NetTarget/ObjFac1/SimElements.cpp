// SimElements.cpp -- see SimElements.h.
//
// The behaviour is that of the original ObjFac1 elements (Missile.cpp, Mine.cpp, PowerUp.cpp, BumperGate.cpp),
// which the golden hashes in docs/determinism-golden.txt and docs/physics-golden.txt pin down. The missile's maths is
// the original's exactly (docs/legacy-element-differences.md).
#include "StdAfx.h"

#include <algorithm>

#include "SimElements.h"
#include "DoubleSpeedSource.h"
#include "FinishLine.h"
#include "FuelSource.h"
#include "../Model/FreeElementMovingHelper.h"
#include "../Model/Level.h"
#include "../Model/ShapeCollisions.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/WorldCoordinates.h"

// ---- surfaces --------------------------------------------------------------------------------------------------

MR_SimSurface::MR_SimSurface( const MR_ObjectFromFactoryId& pId )
    : MR_SurfaceElement( pId )
{
}

// Mirrors MR_BitmapSurface::GetEffectList(): every textured wall or floor carries an infinite-weight
// MR_PhysicalCollision so MainCharacter::ApplyEffect() bounces off it. Without it craft would stop dead at walls.
const MR_ContactEffectList* MR_SimSurface::GetEffectList()
{
    static MR_PhysicalCollision sEffect;
    static MR_ContactEffectList sEffectList;
    static bool sInitialized = false;
    if( !sInitialized ) {
        sInitialized = true;
        sEffect.mWeight = MR_InertialMoment::eInfiniteWeight;
        sEffect.mXSpeed = 0;
        sEffect.mYSpeed = 0;
        sEffect.mZSpeed = 0;
        sEffectList.AddTail( &sEffect );
    }
    return &sEffectList;
}

// ---- power-up --------------------------------------------------------------------------------------------------

MR_SimPowerUp::MR_SimPowerUp( const MR_ObjectFromFactoryId& pId )
    : MR_FreeElement( pId )
{
    mEffect.mElementPermId = -1;
    mEffects.AddTail( &mEffect );
}

MR_Int32 MR_SimPowerUp::ZMin() const { return mPosition.mZ - 550; }
MR_Int32 MR_SimPowerUp::ZMax() const { return mPosition.mZ + 550; }
MR_Int32 MR_SimPowerUp::AxisX() const { return mPosition.mX; }
MR_Int32 MR_SimPowerUp::AxisY() const { return mPosition.mY; }
MR_Int32 MR_SimPowerUp::RayLen() const { return 550; }
const MR_ContactEffectList* MR_SimPowerUp::GetEffectList() { return &mEffects; }
const MR_ShapeInterface* MR_SimPowerUp::GetReceivingContactEffectShape() { return this; }

int MR_SimPowerUp::Simulate( MR_SimulationTime pDuration, MR_Level*, int pRoom )
{
    // Just rotate on itself.
    if( pDuration != 0 ) {
        mOrientation = MR_NORMALIZE_ANGLE( mOrientation + pDuration );
    }
    return pRoom;
}

BOOL MR_SimPowerUp::AssignPermNumber( int pNumber )
{
    mEffect.mElementPermId = pNumber;
    return TRUE;
}

// ---- bumper gate -----------------------------------------------------------------------------------------------

MR_SimBumperGate::MR_SimBumperGate( const MR_ObjectFromFactoryId& pId )
    : MR_FreeElement( pId ), mTimeSinceLastCollision( 1000000 ), mLastState( kBumperGateFrames - 1 ),
      mCurrentFrame( kBumperGateFrames - 1 )
{
    mEffects.AddTail( &mCollisionEffect );
}

MR_Int32 MR_SimBumperGate::ZMin() const { return mPosition.mZ + 2; }
MR_Int32 MR_SimBumperGate::ZMax() const { return mPosition.mZ + 1500 + mCurrentFrame * (3000 - 1500) / std::max( 1, mLastState ); }
MR_Int32 MR_SimBumperGate::AxisX() const { return mPosition.mX; }
MR_Int32 MR_SimBumperGate::AxisY() const { return mPosition.mY; }
MR_Int32 MR_SimBumperGate::RayLen() const { return 200 + mCurrentFrame * (2500 - 200) / std::max( 1, mLastState ); }

const MR_ContactEffectList* MR_SimBumperGate::GetEffectList()
{
    mCollisionEffect.mWeight = MR_PhysicalCollision::eInfiniteWeight;
    mCollisionEffect.mXSpeed = 0;
    mCollisionEffect.mYSpeed = 0;
    mCollisionEffect.mZSpeed = 0;
    return &mEffects;
}

const MR_ShapeInterface* MR_SimBumperGate::GetGivingContactEffectShape() { return nullptr; }
const MR_ShapeInterface* MR_SimBumperGate::GetReceivingContactEffectShape() { return this; }

int MR_SimBumperGate::Simulate( MR_SimulationTime pDuration, MR_Level*, int pRoom )
{
    if( pDuration >= 0 ) {
        mTimeSinceLastCollision += pDuration;
        if( mTimeSinceLastCollision < 1500 ) {
            mCurrentFrame = ( 1500 - mTimeSinceLastCollision ) * mLastState / 1500;
        }
        else if( mTimeSinceLastCollision < 9000 ) {
            mCurrentFrame = 0;
        }
        else if( mTimeSinceLastCollision < 13000 ) {
            mCurrentFrame = ( mTimeSinceLastCollision - 9000 ) * mLastState / 4000;
        }
        else {
            mCurrentFrame = mLastState;
        }
    }
    return pRoom;
}

void MR_SimBumperGate::ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime, MR_SimulationTime, BOOL, MR_Angle,
                                    MR_Int32, MR_Int32, MR_Level* )
{
    if( dynamic_cast<const MR_PhysicalCollision*>( pEffect ) == nullptr ) {
        return;
    }
    mTimeSinceLastCollision = mCurrentFrame >= mLastState || mLastState == 0 ? 0 : 1500 - 1500 * mCurrentFrame / mLastState;
}

// ---- missile ---------------------------------------------------------------------------------------------------

MR_SimMissile::MR_SimMissile( const MR_ObjectFromFactoryId& pId )
    : MR_FreeElement( pId ), mHoverId( -1 ), mLived( 0 ), mXSpeed( 0 ), mYSpeed( 0 ), mBounceSoundEvent( FALSE )
{
    mEffects.AddTail( &mCollisionEffect );
    mEffects.AddTail( &mLostOfControlEffect );
    mLostOfControlEffect.mType = MR_LostOfControl::eMissile;
    mLostOfControlEffect.mElementId = -1;
    mLostOfControlEffect.mHoverId = mHoverId;
}

MR_Int32 MR_SimMissile::ZMin() const { return mPosition.mZ - 300; }
MR_Int32 MR_SimMissile::ZMax() const { return mPosition.mZ + 300; }
MR_Int32 MR_SimMissile::AxisX() const { return mPosition.mX; }
MR_Int32 MR_SimMissile::AxisY() const { return mPosition.mY; }
MR_Int32 MR_SimMissile::RayLen() const { return 300; }

void MR_SimMissile::SetOwnerId( int pOwner )
{
    mHoverId = pOwner;
    mLostOfControlEffect.mHoverId = pOwner;
}

const MR_ContactEffectList* MR_SimMissile::GetEffectList()
{
    if( mLived <= 175 ) {
        return nullptr;
    }
    mCollisionEffect.mWeight = 100;
    mCollisionEffect.mXSpeed = mXSpeed * 256;
    mCollisionEffect.mYSpeed = mYSpeed * 256;
    mCollisionEffect.mZSpeed = 0;
    return &mEffects;
}

const MR_ShapeInterface* MR_SimMissile::GetGivingContactEffectShape() { return this; }
const MR_ShapeInterface* MR_SimMissile::GetReceivingContactEffectShape() { return this; }

void MR_SimMissile::ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime, MR_SimulationTime, BOOL pValidDirection,
                                 MR_Angle pHorizontalDirection, MR_Int32, MR_Int32, MR_Level* )
{
    const MR_PhysicalCollision* lCollision = dynamic_cast<const MR_PhysicalCollision*>( pEffect );
    if( lCollision == nullptr || !pValidDirection ) {
        return;
    }

    if( lCollision->mWeight < MR_PhysicalCollision::eInfiniteWeight ) {
        // Hit a free object: the missile dies.
        if( mLived >= 175 ) {
            mLived = 8700;
        }
        return;
    }

    // Hit the structure: a perfect bounce.
    const MR_Angle lDifference = MR_NORMALIZE_ANGLE( pHorizontalDirection - mOrientation + MR_PI );
    if( lDifference < MR_PI / 2 || lDifference > MR_PI + MR_PI / 2 ) {
        mOrientation = MR_NORMALIZE_ANGLE( pHorizontalDirection + lDifference );
        mBounceSoundEvent = TRUE;
    }
}

// Movement is the original missile's, exactly: 5 ms slices, a double speed of 21 * 2222 / 1000 = 46.662 units per ms,
// and a halving search for the furthest free position.
int MR_SimMissile::InternalSimulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{
    mLived += pDuration;

    const double lSteadySpeed = 21.0 * 2222.0 / 1000.0;
    double lSpeed = lSteadySpeed;
    if( mLived > 7500 ) {
        lSpeed = ( 1200 - mLived - 7500 ) * lSteadySpeed / 1200;
        if( lSpeed < 0 ) {
            lSpeed = 0;
        }
    }

    MR_Cylinder lShape;
    lShape.mRayLen = 1;
    lShape.mZMin = mPosition.mZ - 300;
    lShape.mZMax = mPosition.mZ + 300;

    mXSpeed = lSpeed * MR_Cos[mOrientation] / MR_TRIGO_FRACT;
    mYSpeed = lSpeed * MR_Sin[mOrientation] / MR_TRIGO_FRACT;

    MR_2DCoordinate lTranslation;
    lTranslation.mX = pDuration * mXSpeed;
    lTranslation.mY = pDuration * mYSpeed;

    MR_ObstacleCollisionReport lReport;
    MR_SimulationTime lAttempted = pDuration;
    lShape.mAxis.mX = mPosition.mX + lTranslation.mX;
    lShape.mAxis.mY = mPosition.mY + lTranslation.mY;

    while( true ) {
        bool lSuccess = false;
        lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );
        if( lReport.IsInMaze() && !lReport.HaveContact() ) {
            mPosition.mX = lShape.mAxis.mX;
            mPosition.mY = lShape.mAxis.mY;
            pRoom = lReport.Room();
            lSuccess = true;
        }

        if( lAttempted < 6 ) {
            break;
        }
        if( lSuccess ) {
            if( lAttempted == pDuration ) {
                break;
            }
            lAttempted /= 2;
            lShape.mAxis.mX += lAttempted * lTranslation.mX / pDuration;
            lShape.mAxis.mY += lAttempted * lTranslation.mY / pDuration;
        }
        else {
            lAttempted /= 2;
            lShape.mAxis.mX -= lAttempted * lTranslation.mX / pDuration;
            lShape.mAxis.mY -= lAttempted * lTranslation.mY / pDuration;
        }
    }
    return pRoom;
}

int MR_SimMissile::Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{
    while( pDuration > 0 ) {
        pRoom = InternalSimulate( pDuration > 5 ? 5 : pDuration, pLevel, pRoom );
        pDuration -= 5;
    }
    return mLived >= 8700 ? MR_Level::eMustBeDeleted : pRoom;
}

// ---- mine ------------------------------------------------------------------------------------------------------

MR_SimMine::MR_SimMine( const MR_ObjectFromFactoryId& pId )
    : MR_FreeElement( pId ), mOnGround( FALSE )
{
    mEffects.AddTail( &mLostOfControlEffect );
    mLostOfControlEffect.mType = MR_LostOfControl::eMine;
    mLostOfControlEffect.mElementId = -1;
    mLostOfControlEffect.mHoverId = -1;
    mOrientation = 0;
}

MR_Int32 MR_SimMine::ZMin() const { return mPosition.mZ; }
MR_Int32 MR_SimMine::ZMax() const { return mPosition.mZ + 140; }
MR_Int32 MR_SimMine::AxisX() const { return mPosition.mX; }
MR_Int32 MR_SimMine::AxisY() const { return mPosition.mY; }
MR_Int32 MR_SimMine::RayLen() const { return 400; }
const MR_ContactEffectList* MR_SimMine::GetEffectList() { return mOnGround ? &mEffects : nullptr; }
const MR_ShapeInterface* MR_SimMine::GetGivingContactEffectShape() { return nullptr; }
const MR_ShapeInterface* MR_SimMine::GetReceivingContactEffectShape() { return this; }

BOOL MR_SimMine::AssignPermNumber( int pNumber )
{
    mLostOfControlEffect.mElementId = pNumber;
    return TRUE;
}

int MR_SimMine::Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{
    if( pRoom == -1 ) {
        mOnGround = FALSE;
    }
    else if( !mOnGround && pDuration > 0 && pLevel != nullptr ) {
        mPosition.mZ -= pDuration * 0.6;
        MR_ObstacleCollisionReport lReport;
        lReport.GetContactWithObstacles( pLevel, this, pRoom, this );
        if( !lReport.IsInMaze() ) {
            mOnGround = TRUE;
        }
        else if( lReport.HaveContact() ) {
            mPosition.mZ += lReport.StepHeight();
            mOnGround = TRUE;
        }
    }
    return pRoom;
}

// ---- test element ----------------------------------------------------------------------------------------------

MR_SimTestElement::MR_SimTestElement( const MR_ObjectFromFactoryId& pId )
    : MR_FreeElement( pId ), mXSpeed( 0 ), mYSpeed( 0 )
{
    mCollisionShape.mRay = 250;
    mContactShape.mRay = 300;
    mContactEffect.mWeight = 90;
    mContactEffectList.AddTail( &mContactEffect );
}

int MR_SimTestElement::Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{
    const MR_3DCoordinate lTranslation( ( mXSpeed * static_cast<int>( pDuration ) ) / 256,
                                        ( mYSpeed * static_cast<int>( pDuration ) ) / 256,
                                        ( -2 * 256 * static_cast<int>( pDuration ) ) / 256 );
    MR_3DCoordinate lNewPosition = mPosition;
    lNewPosition.mX += lTranslation.mX;
    lNewPosition.mY += lTranslation.mY;
    lNewPosition.mZ += lTranslation.mZ;

    Cylinder lShape;
    lShape.mRay = mCollisionShape.mRay;
    lShape.mPosition = lNewPosition;

    MR_ObstacleCollisionReport lReport;
    lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );
    if( lReport.IsInMaze() ) {
        if( !lReport.HaveContact() ) {
            mPosition = lNewPosition;
            pRoom = lReport.Room();
        }
        else if( lReport.SpaceToCeiling() > 0 && lReport.StepHeight() < 100 ) {
            lNewPosition.mZ += lReport.StepHeight() + 1;
            lShape.mPosition = lNewPosition;
            lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );
            if( lReport.IsInMaze() && !lReport.HaveContact() ) {
                mPosition = lNewPosition;
                pRoom = lReport.Room();
            }
        }
    }
    return pRoom;
}

const MR_ShapeInterface* MR_SimTestElement::GetObstacleShape()
{
    mCollisionShape.mPosition = mPosition;
    return &mCollisionShape;
}

void MR_SimTestElement::ApplyEffect( const MR_ContactEffect* pEffect, MR_SimulationTime, MR_SimulationTime, BOOL pValidDirection,
                                     MR_Angle pHorizontalDirection, MR_Int32, MR_Int32, MR_Level* )
{
    const MR_PhysicalCollision* lCollision = dynamic_cast<const MR_PhysicalCollision*>( pEffect );
    if( lCollision != nullptr && pValidDirection ) {
        MR_InertialMoment lMoment;
        lMoment.mWeight = 90;
        lMoment.mXSpeed = mXSpeed;
        lMoment.mYSpeed = mYSpeed;
        lMoment.mZSpeed = 0;
        lMoment.ComputeCollision( lCollision, pHorizontalDirection );
        mXSpeed = lMoment.mXSpeed;
        mYSpeed = lMoment.mYSpeed;
    }
}

const MR_ContactEffectList* MR_SimTestElement::GetEffectList()
{
    mContactEffect.mXSpeed = mXSpeed;
    mContactEffect.mYSpeed = mYSpeed;
    mContactEffect.mZSpeed = 0;
    return &mContactEffectList;
}

const MR_ShapeInterface* MR_SimTestElement::GetReceivingContactEffectShape()
{
    mCollisionShape.mPosition = mPosition;
    return &mCollisionShape;
}

const MR_ShapeInterface* MR_SimTestElement::GetGivingContactEffectShape()
{
    mContactShape.mPosition = mPosition;
    return &mContactShape;
}

// ---- registry --------------------------------------------------------------------------------------------------

MR_ObjectFromFactory* MR_CreateSimElement( MR_UInt16 pClassId )
{
    const MR_ObjectFromFactoryId lId = { 1, pClassId };
    switch( pClassId ) {
        case 10:
        case 13: return new MR_SimTestElement( lId );
        case 150: return new MR_SimMissile( lId );
        case 151: return new MR_SimMine( lId );
        case 152: return new MR_SimPowerUp( lId );
        case 170: return new MR_SimBumperGate( lId );
        case 200: return new MR_FuelSource( lId );
        case 201: return new MR_DoubleSpeedSource( lId );
        case 202: return new MR_FinishLine( lId, MR_CheckPoint::eFinishLine );
        case 203: return new MR_FinishLine( lId, MR_CheckPoint::eCheck1 );
        case 204: return new MR_FinishLine( lId, MR_CheckPoint::eCheck2 );
        default: break;
    }
    // Plain-texture surfaces: the classic ones (1 to 5) and the track set (50 to 73).
    if( ( pClassId >= 1 && pClassId <= 5 ) || ( pClassId >= 50 && pClassId <= 73 ) ) {
        return new MR_SimSurface( lId );
    }
    return nullptr;
}

void MR_RegisterSimElements()
{
    MR_DllObjectFactory::RegisterLocalDll( 1, MR_CreateSimElement );
}

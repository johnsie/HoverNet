// MainCharacter.cpp
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which 
// you have taken this file. If you can not find the license you can not use 
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions 
// and limitations under the License.
//

#include "StdAfx.h"

#include <math.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "MainCharacter.h"
#include "../Model/RaceEffects.h"
#include "../Model/FreeElementMovingHelper.h"
#include "../Util/FuzzyLogic.h"

#define MR_NB_HOVER_MODEL 8

// Resource IDs for actor models (from ObjFac1Res.h)
#define MR_ELECTRO_CAR  10
#define MR_HITECH_CAR   11
#define MR_BITURBO_CAR  12
#define MR_EON_CRAFT  19
#define MR_MANTA_CRAFT 24
#define MR_SELECTABLE_HOVER_MODEL_COUNT 5

// Local types
class MR_MainCharacterState: private MR_BitPack
{
   friend class MR_MainCharacter;

   // Packing description
   //                     Offset  len   Prec
   #define  MC_POSX           0,   26,     5
   #define  MC_POSY          26,   26,     5
   #define  MC_POSZ          52,   15,     0
   #define  MC_ROOM          67,   11,     0
   #define  MC_ORIENTATION   78,    9,     3
   #define  MC_SPEED_X_256   87,   13,     2
   #define  MC_SPEED_Y_256  100,   13,     2
   #define  MC_SPEED_Z_256  113,    9,     2
   #define  MC_CONTROL_ST   122,   15,     0
   #define  MC_ON_FLOOR     137,    1,     0
   #define  MC_HOVER_MODEL  138,    3,     0
   #define  MC_PADDING      141,   11,     0 
   //   #define  MC_SOUNDFX      141,    5,     0 

   // Total state storage is the deployed 20-byte MR_BitPack representation.

   public:

};

static_assert( sizeof( MR_MainCharacterState ) == 20,
               "Main character network state must remain wire-compatible" );



// Local constants
#define TIME_SLICE                     5
#define MINIMUM_SPLITTABLE_TIME_SLICE  6
  

const int eCharacterMovementRay = 1100;
const int eCharacterRay         = 1300;
const int eCharacterHeight      = 1500;
const int eCharacterContactRay  = 1450;
const int eCharacterWeight[MR_NB_HOVER_MODEL] = {300, 250, 450, 300, 240, 300, 300, 300 };
const int eMissileRefillTime    = 10000;
const int eMantaJumpRefillTime  = 400;
const int eMantaMissileRefillTime = 150;
const int ePwrUpDuration        = 5000;

const double eSteadySpeed[MR_NB_HOVER_MODEL] = { 8.7*2222.0/1000.0, 
                                                 11.5*2222.0/1000.0,
                                                 10.5*2222.0/1000.0,
                                                 8.7*2222.0/1000.0, 
                                                 10.0*2222.0/1000.0,
                                                 8.7*2222.0/1000.0, 
                                                 8.7*2222.0/1000.0, 
                                                 8.1*2222.0/1000.0
                                               };
const double eMaxZSpeed[MR_NB_HOVER_MODEL]   = {2900.0/1000.0, 
                                                2900.0/900.0,
                                                2900.0/1000.0,
                                                2900.0/1000.0,
                                                2900.0/1000.0,
                                                2900.0/1000.0,
                                                2900.0/1000.0,
                                                2900.0/1000.0
                                               };

const double eRotationSpeed                  = MR_PI/1.4/1000.0;

const double eFrictionAccell[MR_NB_HOVER_MODEL] = {-eSteadySpeed[0]/4.0/1000.0,
                                                   -eSteadySpeed[0]/4.0/1200.0,
                                                   -eSteadySpeed[0]/4.0/900.0,
                                                   -eSteadySpeed[0]/4.0/1000.0,
                                                   -eSteadySpeed[4]/4.0/800.0,
                                                   -eSteadySpeed[0]/4.0/1000.0,
                                                   -eSteadySpeed[0]/4.0/1000.0,
                                                   -eSteadySpeed[0]/4.0/700.0
                                                  };

const double eMotorAccell[MR_NB_HOVER_MODEL]    = { eSteadySpeed[0]/1000.0,
                                                    eSteadySpeed[0]/1400.0, 
                                                    eSteadySpeed[2]/1050.0,
                                                    eSteadySpeed[0]/1000.0,
                                                    eSteadySpeed[4]/850.0,
                                                    eSteadySpeed[0]/1000.0,
                                                    eSteadySpeed[0]/1000.0,
                                                    eSteadySpeed[7]/750.0
                                                   };

const double eZAccell[MR_NB_HOVER_MODEL]        = {-eMaxZSpeed[0]/1000.0, 
                                                   -eMaxZSpeed[1]/1000.0, 
                                                   -eMaxZSpeed[2]/850.0,
                                                   -eMaxZSpeed[0]/1000.0, 
                                                   -eMaxZSpeed[0]/1000.0, 
                                                   -eMaxZSpeed[0]/1000.0, 
                                                   -eMaxZSpeed[0]/1000.0, 
                                                   -eMaxZSpeed[7]/1000.0
                                                  };

// const double eBreakAccell             = -eSteadySpeed*3.0/1000.0;
// const double eFrictionRotationAccell  = -eSteadyRotationSpeed*6.0/1000.0;
// const double eBreakRotationSpeedAccell= -eSteadyRotationSpeed*12.0/1000.0;
// const double eJetRotationSpeedAccell  =  eSteadyRotationSpeed*12.0/1000.0;

const double eFuelCapacity                     = 3*60*1000;// 3 minutes of fuel
const double eFuelConsuming[MR_NB_HOVER_MODEL] = { 1.0, 
                                                  0.70,
                                                  2.0,
                                                  1.0,
                                                  1.0,
                                                  1.0,
                                                  1.0,
                                                  1.1,
                                                 };





// Functions implementations

MR_MainCharacter::MR_MainCharacter( const MR_ObjectFromFactoryId& pId )
                 :MR_FreeElement( pId )
{
   mMasterMode           = TRUE;
   mRoom                 = -1;
   mHoverModel           = 0;    // Basic model
   mNetPriority          = FALSE;
   mLastCollisionTime    = 0;

   mRenderer             = NULL;

   mControlState         = 0;
   mMotorOnState         = FALSE;
   mMotorDisplay         = 0;

   mXSpeed                = 0;
   mYSpeed                = 0;
   mZSpeed                = 0;
   mXSpeedBeforeCollision = 0;
   mYSpeedBeforeCollision = 0;
   mBlockedSteps          = 0;
   mOnFloor               = FALSE;
   mCabinOrientation      = 0; // mOrientation; mOrientation is not set yet
   mOutOfControlDuration  = 0;
   mMissileRefillDuration = 0;
   mJumpRefillDuration    = 0;

   mFireDone         = FALSE;

   mCurrentWeapon    = eMissile;
   mPowerUpLeft      = 0;

   mFuelLevel        = eFuelCapacity;
   
   // CRITICAL FIX: Initialize mPosition to valid values (0,0,0) instead of garbage
   mPosition.mX = 0.0;
   mPosition.mY = 0.0;
   mPosition.mZ = 0.0;

   mCollisionShape.mRay = eCharacterRay;
   mContactShape.mRay   = eCharacterContactRay;

   mContactEffectList.AddTail( &mContactEffect );

   mNbLapForRace       = 5; // Hard coded default
   mLapCount           = 0;
   mLastLapCompletion  = 0;
   mLastLapDuration    = 0;
   mBestLapDuration    = 0;

   mHoverId = 10;

   mCheckPoint1 = FALSE;
   mCheckPoint2 = FALSE;

}

MR_MainCharacter::~MR_MainCharacter()
{
   ReleaseRenderer();
}

void MR_MainCharacter::SetAsMaster()
{
   mMasterMode = TRUE;
}

void MR_MainCharacter::SetAsSlave()
{
   mMasterMode = FALSE;
}

void MR_MainCharacter::SetHoverId( int pId )
{
   mHoverId = pId;
}

void MR_MainCharacter::SetHoverModel( int pModel )
{
   mHoverModel = pModel;
}

int  MR_MainCharacter::GetHoverModel()const
{
   return mHoverModel;
}


void MR_MainCharacter::TriggerOutOfControl()
{
   mOutOfControlDuration = 2000;
   mNetPriority = TRUE;
}

void MR_MainCharacter::SetOrientation( MR_Angle pOrientation )
{
   mOrientation      = pOrientation;
   mCabinOrientation = pOrientation;
}


double MR_MainCharacter::GetNormalisedSpeed() const
{
   return sqrt( mXSpeed*mXSpeed + mYSpeed*mYSpeed )/(eSteadySpeed[0]);
}

int MR_MainCharacter::GetHoverId()const
{
   return mHoverId;
}

MR_ObjectFromFactory* MR_MainCharacter::FactoryFunc( MR_UInt16 pClassId )
{
   if( pClassId == 100 )
   {
      // Class 100 is the renderer for MainCharacter
      // PRIORITY: Use ObjFac1 HoverRender - never fall back to SimpleRenderer
      MR_ObjectFromFactoryId lHoverRenderId = { 1, 100 };  // ObjFac1 HoverRender
      
      MR_ObjectFromFactory* pHoverRender = NULL;
      
      // Try to create HoverRender from ObjFac1
      TRY
      {
         pHoverRender = MR_DllObjectFactory::CreateObject( lHoverRenderId );
      }
      CATCH_ALL(e)
      {
         pHoverRender = NULL;
         e->Delete();
      }
      END_CATCH_ALL
      
      if( pHoverRender != NULL )
      {
         return pHoverRender;
      }
      else
      {
         // Failed to create renderer
         return NULL;
      }
   }
   else if( pClassId == MR_MAIN_CHARACTER_CLASS_ID )
   {
      // Return the MainCharacter itself (default behavior)
      MR_ObjectFromFactoryId lId = { MR_MAIN_CHARACTER_DLL_ID, MR_MAIN_CHARACTER_CLASS_ID };
      MR_MainCharacter* pChar = new MR_MainCharacter( lId );
      return pChar;
   }
   
   return NULL;
}

void MR_MainCharacter::RegisterFactory()
{
   MR_DllObjectFactory::RegisterLocalDll( MR_MAIN_CHARACTER_DLL_ID, FactoryFunc );
}



MR_MainCharacter* MR_MainCharacter::New( int pNbLap, BOOL pAllowWeapons )
{
   MR_ObjectFromFactoryId lId = { MR_MAIN_CHARACTER_DLL_ID, MR_MAIN_CHARACTER_CLASS_ID };

   MR_MainCharacter* lReturnValue = (MR_MainCharacter*) MR_DllObjectFactory::CreateObject( lId );

   if( lReturnValue != NULL )
   {
      lReturnValue->mNbLapForRace = pNbLap;
      lReturnValue->mAllowWeapons = pAllowWeapons;
   }
   return lReturnValue;
}

MR_ElementNetState MR_MainCharacter::GetNetState()const
{
   static MR_MainCharacterState lsState; // Static is ok because the variable will be used immediatly

   MR_ElementNetState lReturnValue;

   lReturnValue.mDataLen = sizeof( lsState );
   lReturnValue.mData    = (MR_UInt8*)&lsState;

   lsState.Clear( sizeof( lsState ) );

   lsState.Set( MC_POSX, mPosition.mX );
   lsState.Set( MC_POSY, mPosition.mY );
   lsState.Set( MC_POSZ, mPosition.mZ );

   lsState.Set( MC_ROOM, mRoom );
   
   lsState.Set( MC_ORIENTATION, mOrientation );

   lsState.Set( MC_SPEED_X_256, mXSpeedBeforeCollision*256 );
   lsState.Set( MC_SPEED_Y_256, mYSpeedBeforeCollision*256 );
   lsState.Set( MC_SPEED_Z_256, mZSpeed*256 );

   lsState.Set( MC_CONTROL_ST, mControlState );
   lsState.Set( MC_ON_FLOOR,   mOnFloor      );
   lsState.Set( MC_HOVER_MODEL,mHoverModel    );
   int lOutOfControlState = mOutOfControlDuration/4;
   if( lOutOfControlState < 0 ) lOutOfControlState = 0;
   if( lOutOfControlState > 0x7FF ) lOutOfControlState = 0x7FF;
   lsState.Set( MC_PADDING, lOutOfControlState );

   /*
   lsState.mPosX          = mPosition.mX;
   lsState.mPosY          = mPosition.mY;
   lsState.mPosZ          = mPosition.mZ;

   // lsState.mRoom          = mRoom;

   lsState.mOrientation   = mOrientation;

   lsState.mXSpeed_256    = mXSpeed*256;
   lsState.mYSpeed_256    = mYSpeed*256;
   lsState.mZSpeed_256    = mZSpeed*256;

   lsState.mControlState  = mControlState;

   lsState.mOnFloor       = mOnFloor;
   */
   return lReturnValue;

}

void MR_MainCharacter::SetNetState( int pDataLen, const MR_UInt8* pData )
{
   // CRITICAL FIX: Validate buffer size before casting to MR_MainCharacterState
   // The MR_BitPack::Get() method can read up to 4 bytes past the declared
   // 18-byte mFieldList array, potentially reading adjacent memory
   if( pDataLen < (int)sizeof( MR_MainCharacterState ) )
   {
      ASSERT( FALSE );  // Network message too small!
      return;
   }

   const MR_MainCharacterState* lState = (const MR_MainCharacterState*)pData;

   mPosition.mX = lState->Get( MC_POSX );
   mPosition.mY = lState->Get( MC_POSY );
   mPosition.mZ = lState->Get( MC_POSZ );

   mRoom = lState->Get( MC_ROOM );

   if( mRoom < -1 )
   {
      mRoom = lState->Getu( MC_ROOM );
   }
   
   mOrientation = lState->Getu( MC_ORIENTATION );

   mXSpeed = lState->Get( MC_SPEED_X_256 )/256.0;
   mYSpeed = lState->Get( MC_SPEED_Y_256 )/256.0;
   mZSpeed = lState->Get( MC_SPEED_Z_256 )/256.0;

   mControlState = lState->Getu( MC_CONTROL_ST );
   mOnFloor      = lState->Get( MC_ON_FLOOR );
   mHoverModel   = lState->Getu( MC_HOVER_MODEL );
   mOutOfControlDuration = lState->Getu( MC_PADDING )*4;


   /*
   mPosition.mX = lState->mPosX;
   mPosition.mY = lState->mPosY;         
   mPosition.mZ = lState->mPosZ;         

   // mRoom        = lState->mRoom;         

   mOrientation = lState->mOrientation;  

   mXSpeed      = lState->mXSpeed_256/256.0;   
   mYSpeed      = lState->mYSpeed_256/256.0;   
   mZSpeed      = lState->mZSpeed_256/256.0;   

   mControlState= lState->mControlState; 

   mOnFloor     = lState->mOnFloor;      
   */

   // Determine cabin orientation
   if( mControlState & eBreakDirection )
   {
      mCabinOrientation = MR_NORMALIZE_ANGLE( RAD_2_MR_ANGLE( atan2(mYSpeed, mXSpeed) )+MR_PI );
   }
   else if( (mControlState & eStraffleRight)^(mControlState & eStraffleLeft))
   {
      if( mControlState & eStraffleRight )
      {
         mCabinOrientation = MR_NORMALIZE_ANGLE( mOrientation - MR_PI/2 );
      }
      else
      {
         mCabinOrientation = MR_NORMALIZE_ANGLE( mOrientation + MR_PI/2 );
      }
   }
}


void MR_MainCharacter::SetNbLapForRace( int pNbLap )
{
   mNbLapForRace = pNbLap;
}



void MR_MainCharacter::SetControlState( int pState, MR_SimulationTime pTime )
{
   int lState = pState;

   if( pTime < 0 )
   {
      BOOL bRightPressed = (pState & eRight) && !(mControlState & eRight);
      BOOL bLeftPressed = (pState & eLeft) && !(mControlState & eLeft);

      if( bRightPressed )
      {
         mHoverModel++;
         mHoverModel = ((mHoverModel % MR_SELECTABLE_HOVER_MODEL_COUNT) + MR_SELECTABLE_HOVER_MODEL_COUNT) % MR_SELECTABLE_HOVER_MODEL_COUNT;
      }
      
      if( bLeftPressed )
      {
         mHoverModel--;
         mHoverModel = ((mHoverModel % MR_SELECTABLE_HOVER_MODEL_COUNT) + MR_SELECTABLE_HOVER_MODEL_COUNT) % MR_SELECTABLE_HOVER_MODEL_COUNT;
      }
   }

   // First verify transition states
   if( mAllowWeapons && !(mControlState & eSelectWeapon)&&(lState & eSelectWeapon) )
   {
      // Step the enum through an int value, not a type-punned int* alias of it.
      int lNextWeapon = static_cast<int>( mCurrentWeapon ) + 1;
      if( lNextWeapon == static_cast<int>( eNotAWeapon ) )
      {
         lNextWeapon = 0;
      }
      mCurrentWeapon = static_cast<eWeapon>( lNextWeapon );
   }
   if( !(mControlState & eFire)&&(lState & eFire) )
   {
      mFireDone = FALSE;
   }

   if( !(mControlState & eJump)&&(lState & eJump) )
   {
      const BOOL lMantaJumpReady = (mHoverModel == 4) && (mJumpRefillDuration == 0);
      if( mOnFloor || lMantaJumpReady )
      {
         mZSpeed = 1.1*eMaxZSpeed[mHoverModel];
         if( mHoverModel == 4 )
         {
            mJumpRefillDuration = eMantaJumpRefillTime;
         }

         QueueSound( eSoundJump, TRUE, FALSE );
      }
      else
      {
         QueueSound( eSoundMisJump, TRUE, FALSE );
      }
   }

   if( !(mControlState&(eRight|eLeft)) )
   {
      lState |= eSlowRotation;
   }

   // Now verify continous states
   if( mFuelLevel <= 0.0 )
   {
      if( (!mMotorOnState)&&(lState&eMotorOn) )
      {
         mFuelLevel = 120;
      }
      else
      {
         lState &= ~eMotorOn;
      }
   }

   mMotorOnState = pState&eMotorOn;

   mControlState = lState;
}

 

int MR_MainCharacter::Simulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{   

   mRoom = pRoom;

   if( pDuration > 0 )
   {
      if( mMasterMode )
      {
         if( (mControlState & eMotorOn)&&(mFuelLevel > 0.0) )
         {
            mMotorDisplay = 250;
         }
      }
      else
      {
         if( mControlState & eMotorOn )
         {
            mMotorDisplay = 250;
         }
      }
   }

   mMotorDisplay -= pDuration;

   mJumpRefillDuration -= pDuration;
   if( mJumpRefillDuration < 0 )
   {
      mJumpRefillDuration = 0;
   }

   // The Manta can sustain its jump control in the air. This is deliberately
   // handled during simulation (not only on the key-down edge), so holding the
   // button produces another jump as soon as its short re-arm expires.
   if( mHoverModel == 4 && (mControlState & eJump) && mJumpRefillDuration == 0 )
   {
      mZSpeed = 1.1*eMaxZSpeed[mHoverModel];
      mJumpRefillDuration = eMantaJumpRefillTime;
      QueueSound( eSoundJump, TRUE, FALSE );
   }

   if( mMotorDisplay < 0 )
   {
      mMotorDisplay = 0;
   }

   // Orient the cabin if a special move have been made

   if( mControlState & eBreakDirection )
   {
      double lAbsoluteSpeed = sqrt( mXSpeed*mXSpeed+mYSpeed*mYSpeed );

      if( lAbsoluteSpeed > (eSteadySpeed[mHoverModel]/20.0) )
      {
         mCabinOrientation = MR_NORMALIZE_ANGLE( RAD_2_MR_ANGLE( atan2(mYSpeed, mXSpeed) )+MR_PI );
      }
   }
   else if( (mControlState & eStraffleRight)^(mControlState & eStraffleLeft))
   {
      if( mControlState & eStraffleRight )
      {
         mCabinOrientation = MR_NORMALIZE_ANGLE( mOrientation - MR_PI/2 );
      }
      else
      {
         mCabinOrientation = MR_NORMALIZE_ANGLE( mOrientation + MR_PI/2 );
      }
   }
   else
   {
      mCabinOrientation = mOrientation;
   }



   if( mMasterMode )
   {
      MR_SimulationTime lDuration = pDuration;

      while( lDuration > 0 )
      {
         if( lDuration > TIME_SLICE )
         {
            pRoom = InternalSimulate( TIME_SLICE, pLevel, pRoom );
         }
         else
         {
            pRoom = InternalSimulate( lDuration, pLevel, pRoom );
         }
         lDuration -= TIME_SLICE;
      }

      mXSpeedBeforeCollision = mXSpeed;
      mYSpeedBeforeCollision = mYSpeed;


      // If the user pressed fire, laught a missile
      mMissileRefillDuration -= pDuration;

      if( mMissileRefillDuration <= 0 )
      {
         mMissileRefillDuration = 0;
      }

      // Unlike the other craft, the Manta repeats fire while the control is
      // held. The short limiter avoids creating one missile per simulation
      // frame while still feeling effectively immediate.
      if( mHoverModel == 4 && (mControlState & eFire) &&
          mCurrentWeapon == eMissile && mMissileRefillDuration == 0 )
      {
         mFireDone = FALSE;
      }

      mPowerUpLeft -= pDuration*eFuelConsuming[mHoverModel];

      if( mPowerUpLeft < 0 )
      {
         mPowerUpLeft = 0;
      }


      if( (mControlState & eFire)&& !mFireDone )
      {
         mFireDone = TRUE;

         if( mCurrentWeapon == eMissile )
         {
            if( (mMissileRefillDuration == 0) && mAllowWeapons )
            {
               mMissileRefillDuration = (mHoverModel == 4) ?
                  eMantaMissileRefillTime : eMissileRefillTime;

               MR_ObjectFromFactoryId lObjectId = { 1, 150 };
               // Create a new missile
               MR_FreeElement* lMissile = (MR_FreeElement*)MR_DllObjectFactory::CreateObject( lObjectId );

               if( lMissile != NULL )
               {
                  lMissile->SetOwnerId( mHoverId );
                  lMissile->mPosition     = mPosition;
                  lMissile->mPosition.mZ += 1100;
                  lMissile->mOrientation  = mCabinOrientation;

                  pLevel->InsertElement( lMissile, mRoom, TRUE );

                  QueueSound( eSoundFire, TRUE, TRUE );

               }
            }
         }
         else if( mAllowWeapons && mCurrentWeapon == eMine )
         {
            if( !mMineList.IsEmpty() )
            {
               MR_3DCoordinate lPos = mPosition;
               lPos.mZ += 800;
               pLevel->SetPermElementPos( mMineList.GetHead(), mRoom, lPos );
               mMineList.Remove();
            }
         }
         else if( mAllowWeapons && mCurrentWeapon == ePowerUp )
         {
            if( !mPowerUpList.IsEmpty() )
            {
               MR_3DCoordinate lPos = mPosition;
               lPos.mZ += 1200;

               pLevel->SetPermElementPos( mPowerUpList.GetHead(), mRoom, lPos );
               mPowerUpList.Remove();

               mPowerUpLeft = ePwrUpDuration;
            }
         }
      }
   }
   else
   {
      // Slave mode
      pRoom = InternalSimulate( pDuration, pLevel, pRoom );
   }
   return pRoom;
}

int MR_MainCharacter::InternalSimulate( MR_SimulationTime pDuration, MR_Level* pLevel, int pRoom )
{
   // Determine new speed (PosVar and OrientationVar
   double lAbsoluteSpeed = sqrt( mXSpeed*mXSpeed+mYSpeed*mYSpeed );

   if( mOutOfControlDuration <= 0 ) // No friction when out of control..it's just more cool
   {
      if( lAbsoluteSpeed <= -pDuration*eFrictionAccell[mHoverModel] )
      {
         mXSpeed = 0;
         mYSpeed = 0;
      }
      else
      {
         double lFrictionAmplifier = 1.0;

         if( lAbsoluteSpeed < eSteadySpeed[mHoverModel]/3 )
         {
            lFrictionAmplifier = 0.4;
         }
         else if( (lAbsoluteSpeed > eSteadySpeed[mHoverModel])&&(lAbsoluteSpeed < 2.5*eSteadySpeed[mHoverModel]) )
         {
            lFrictionAmplifier = (std::min)( 1.7, 1.1+2.5*(lAbsoluteSpeed/eSteadySpeed[0]-1.0) );
         }

         double lConstantPart = pDuration*lFrictionAmplifier*eFrictionAccell[mHoverModel]/lAbsoluteSpeed;

         mXSpeed += lConstantPart*mXSpeed;
         mYSpeed += lConstantPart*mYSpeed;
      }      
   }

   mZSpeed += pDuration*eZAccell[mHoverModel];

   if( mZSpeed < -eMaxZSpeed[mHoverModel] )
   {
      mZSpeed = -eMaxZSpeed[mHoverModel];
   }


   // Apply rotation
   if( mOutOfControlDuration > 0 )
   {
      mOutOfControlDuration -= pDuration;

      mOrientation = MR_NORMALIZE_ANGLE( (int)(mOrientation+pDuration*8*eRotationSpeed) );

   }
   else
   {
      if( (mControlState & eRight)^(mControlState & eLeft))
      {
         double lRotation;

         if( mControlState & eRight )
         {
            lRotation = -pDuration*eRotationSpeed;
         }
         else
         {
            lRotation = pDuration*eRotationSpeed;
         }

         if( mControlState&eSlowRotation)
         {
            lRotation /= 4;
         }

         mOrientation = MR_NORMALIZE_ANGLE( mOrientation+(int)lRotation );
      }
   }


   // MotorEffect
   if( mControlState & eMotorOn )
   {
      double lDirectionalSpeed = (mXSpeed*MR_Cos[ mCabinOrientation ] + mYSpeed*MR_Sin[ mCabinOrientation ])/MR_TRIGO_FRACT;

      double lMaxSpeedFactor = 1.3;

      if( mPowerUpLeft > 0 )
      {
         lMaxSpeedFactor = 1.9;
      }
      
      if( lDirectionalSpeed < (lMaxSpeedFactor*eSteadySpeed[mHoverModel]))
      {
         double lAccelerationFactor = 1.0-lDirectionalSpeed/(lMaxSpeedFactor*1.25*eSteadySpeed[mHoverModel]);
       

         if( lAccelerationFactor<0)
         {
            lAccelerationFactor = 0;
         }
         else if( lAccelerationFactor > 0.8 )
         {
            lAccelerationFactor = 0.8;
         }

         if( mPowerUpLeft > 0 )
         {
            lAccelerationFactor *= 3.2;
         }
         else
         {
            lAccelerationFactor *= 1.8;
         }

         mXSpeed += (pDuration*lAccelerationFactor*eMotorAccell[mHoverModel]*MR_Cos[ mCabinOrientation ])/MR_TRIGO_FRACT;
         mYSpeed += (pDuration*lAccelerationFactor*eMotorAccell[mHoverModel]*MR_Sin[ mCabinOrientation ])/MR_TRIGO_FRACT;
      }


      mFuelLevel -= pDuration*eFuelConsuming[mHoverModel];
   }

   
   // Determine new dispacement



   Cylinder lShape;

   lShape.mRay =  eCharacterMovementRay; //eCharacterRay;

   // Compute speed objectives

   MR_3DCoordinate lTranslation( ( mXSpeed*(int)pDuration),
                                 ( mYSpeed*(int)pDuration),
                                 ( mZSpeed*(int)pDuration) );

   // This avoid continious jumping
   if( lTranslation.mZ == 0 )
   {
      lTranslation.mZ = -1;
   }
   
   // Verify if the move is valid
   MR_ObstacleCollisionReport lReport;


   BOOL              lSuccessfullTry;
   MR_SimulationTime lDuration = pDuration;

   const MR_Int32 lStartX = mPosition.mX;
   const MR_Int32 lStartY = mPosition.mY;

   lShape.mPosition.mX = mPosition.mX+lTranslation.mX;
   lShape.mPosition.mY = mPosition.mY+lTranslation.mY;
   lShape.mPosition.mZ = mPosition.mZ+lTranslation.mZ;

   while(1)
   {      
      lSuccessfullTry = FALSE;

      lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );   

      if( lReport.IsInMaze() )
      {
         if( !lReport.HaveContact() )
         {
            mPosition    = lShape.mPosition;
            pRoom        = lReport.Room();
            mOnFloor     = FALSE;

            lSuccessfullTry = TRUE;

         }
         else
         {         

            // Determine if we can go on the object
            if( (lReport.SpaceToCeiling() > 0 )&&
                (lReport.StepHeight()<= (-lTranslation.mZ)+1 )  )
            {
               lShape.mPosition.mZ += lReport.StepHeight();

               lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );   
   
               if( lReport.IsInMaze() )
               {
                  if( !lReport.HaveContact() )
                  {
                     if( !mOnFloor )
                     {
                        QueueSound( eSoundBump, TRUE, FALSE );
                     }


                     mZSpeed = 0;
                     mOnFloor = TRUE;

                     mPosition    = lShape.mPosition;
                     pRoom        = lReport.Room();

                     lSuccessfullTry = TRUE;
                  }
               }
            }
            else if( (mZSpeed > 0)&&
                     (lReport.SpaceToFloor() > 0 )&&                  
                     (lReport.CeilingStepHeight()<=lTranslation.mZ)  )
            {
               lShape.mPosition.mZ -= lReport.CeilingStepHeight()+1;

               lReport.GetContactWithObstacles( pLevel, &lShape, pRoom, this );   

               if( lReport.IsInMaze() )
               {
                  if( !lReport.HaveContact() )
                  {
                     mZSpeed = 0;
                     mOnFloor = FALSE;

                     mPosition    = lShape.mPosition;
                     pRoom        = lReport.Room();

                     lSuccessfullTry = TRUE;
                  }
               }
            }
         }
      }


      if( lDuration < MINIMUM_SPLITTABLE_TIME_SLICE )
      {
         break;
      }
      else
      {
         if( lSuccessfullTry )
         {
            if( lDuration == pDuration )
            {
               break; // success on first attempt
            }

            lDuration /= 2;

            lShape.mPosition.mX += lDuration*lTranslation.mX/pDuration;
            lShape.mPosition.mY += lDuration*lTranslation.mY/pDuration;
            lShape.mPosition.mZ += lDuration*lTranslation.mZ/pDuration;

         }
         else
         {
            lDuration /= 2;

            lShape.mPosition.mX -= lDuration*lTranslation.mX/pDuration;
            lShape.mPosition.mY -= lDuration*lTranslation.mY/pDuration;
            lShape.mPosition.mZ -= lDuration*lTranslation.mZ/pDuration;
         }
      }

   }


   // Stuck guard. The bounce comes from the contact effect, which only fires when the craft is
   // within the (larger) contact ring of a wall. A fast craft (speed doubler) or an awkward
   // corner can be refused every move while resting just outside that ring, so it sat against
   // the wall instead of bouncing. If horizontal moves keep being rejected, bounce it back.
   if( mMasterMode )
   {
      const bool lMoved = (mPosition.mX != lStartX) || (mPosition.mY != lStartY);
      const double lSpeedSquared = mXSpeed*mXSpeed + mYSpeed*mYSpeed;

      if( lMoved || lSpeedSquared < 0.09 )
      {
         mBlockedSteps = 0;
      }
      else if( ++mBlockedSteps >= 4 )
      {
         mBlockedSteps = 0;
         mXSpeed = -mXSpeed*0.5;
         mYSpeed = -mYSpeed*0.5;
      }
   }

   if( (mFuelLevel < 0.0)&& mMasterMode )
   {
      mControlState &= ~eMotorOn;
   }

   mRoom = pRoom;

   return pRoom;

}




const MR_ShapeInterface* MR_MainCharacter::GetObstacleShape( )
{

   return NULL;

   mCollisionShape.mPosition = mPosition;

   return &mCollisionShape;
}


void MR_MainCharacter::ApplyNetworkMissileHit( int pHoverId, const MR_Level* pLevel )
{
   MR_LostOfControl lHit;
   lHit.mType      = MR_LostOfControl::eMissile;
   lHit.mElementId = -1;
   lHit.mHoverId   = pHoverId;

   // ApplyEffect only mutates pLevel on the eMine branch (SetPermElementPos);
   // an eMissile hit never reaches that branch, so treating this level as
   // mutable here is safe even though the caller only has a const view of it.
   ApplyEffect( &lHit, 0, 0, TRUE, 0, 0, 0, const_cast<MR_Level*>( pLevel ) );
}

void MR_MainCharacter::ApplyEffect( const MR_ContactEffect* pEffect,  MR_SimulationTime pTime, MR_SimulationTime pDuration, BOOL pValidDirection, MR_Angle pHorizontalDirection, MR_Int32 /*pZMin*/, MR_Int32 pZMax, MR_Level* pLevel )
{
   MR_ContactEffect* lEffect = (MR_ContactEffect*)pEffect;
   const MR_PhysicalCollision* lPhysCollision = dynamic_cast<MR_PhysicalCollision*>(lEffect);
   const MR_SpeedDoubler*      lSpeedDoubler  = dynamic_cast<MR_SpeedDoubler*>     (lEffect);
   const MR_FuelGain*          lFuelGain      = dynamic_cast<MR_FuelGain*>         (lEffect);
   const MR_LostOfControl*     lLostOfControl = dynamic_cast<MR_LostOfControl*>    (lEffect);
   const MR_CheckPoint*        lLapCompleted  = dynamic_cast<MR_CheckPoint*>       (lEffect);
   const MR_PowerUpEffect*     lPowerUp       = dynamic_cast<MR_PowerUpEffect*>    (lEffect);
      
   if( (lPhysCollision != NULL)&&pValidDirection )
   {
      /*
      double lInvertDampingFactor = 1.0;
      double lDampingFactor       = 0.0;

      if( pZMax-210 < mPosition.mZ )
      {
         lInvertDampingFactor = (pZMax-mPosition.mZ)/210;
         lDampingFactor       = 1.0-lInvertDampingFactor;

         mZSpeed = max( mZSpeed, 512*lDampingFactor*-eZAccell[mHoverModel] );
      }
      */

      if( pValidDirection )
      {
         MR_InertialMoment lMoment;

         lMoment.mWeight = eCharacterWeight[mHoverModel];
         lMoment.mXSpeed = mXSpeed*256;
         lMoment.mYSpeed = mYSpeed*256;
         lMoment.mZSpeed = 0;

         lMoment.ComputeCollision( lPhysCollision,
                                   pHorizontalDirection );

         // mXSpeed = (lInvertDampingFactor*lMoment.mXSpeed/256.0)+lDampingFactor*mXSpeed;
         // mYSpeed = (lInvertDampingFactor*lMoment.mYSpeed/256.0)+lDampingFactor*mYSpeed;
         mXSpeed = lMoment.mXSpeed/256.0;
         mYSpeed = lMoment.mYSpeed/256.0;

         if( !((lMoment.mXSpeed==0)&&(lMoment.mYSpeed==0)) )
         {
            QueueSound( eSoundBump, TRUE, TRUE );
         }

         if( (lPhysCollision->mXSpeed!=0 || lPhysCollision->mYSpeed!=0) )
         {
            TRACE( "Net priority increase %d\n", mMasterMode );
            mNetPriority       = TRUE;
            mLastCollisionTime = pTime;
         }
      }
   }

   if( lSpeedDoubler != NULL )
   {
      /*
      mXSpeed *= 2;
      mYSpeed *= 2;

      double lAbsSpeed =  sqrt( mXSpeed*mXSpeed+mYSpeed*mYSpeed );

      if( lAbsSpeed > eSteadySpeed/4 )
      {
         mXSpeed = eSteadySpeed*2*mXSpeed/lAbsSpeed; 
         mYSpeed = eSteadySpeed*2*mYSpeed/lAbsSpeed; 
      }
      */

      mXSpeed = (4*eSteadySpeed[0]*MR_Cos[ mCabinOrientation ])/MR_TRIGO_FRACT;
      mYSpeed = (4*eSteadySpeed[0]*MR_Sin[ mCabinOrientation ])/MR_TRIGO_FRACT;
   }

   if( (lFuelGain != NULL) && mMasterMode )
   {
      mFuelLevel += pDuration * lFuelGain->mFuelQty;

      if( mFuelLevel > eFuelCapacity )
      {
         mFuelLevel = eFuelCapacity;
      }
   }

   if( lLostOfControl != NULL )
   {

      if( mOutOfControlDuration < 1750 && !mLastHits.Full() )
      {
         mLastHits.Add( lLostOfControl->mHoverId );
      }

      // Apply the visible reaction on both authoritative and replicated craft.
      // Previously slave craft ignored missile effects, so the shooter saw a
      // missile disappear on impact while the remote hovercraft did nothing.
      TriggerOutOfControl();

      QueueSound( eSoundOutOfControl, TRUE, TRUE );

      if( lLostOfControl->mType == MR_LostOfControl::eMine && mMasterMode )
      {
         mZSpeed = 1.1*eMaxZSpeed[0];

         if( (lLostOfControl->mElementId != -1)&&!mMineList.Full() )
         {
            mMineList.Add( lLostOfControl->mElementId );                
            pLevel->SetPermElementPos( lLostOfControl->mElementId, -1, mPosition );
         }
      }
   }

   if( (lPowerUp != NULL) && mMasterMode )
   {
      // TODO Add sound

      if( (mPowerUpLeft == 0)&&(lPowerUp->mElementPermId != -1)&&!mPowerUpList.Full() )
      {
         mPowerUpList.Add( lPowerUp->mElementPermId );                
         pLevel->SetPermElementPos( lPowerUp->mElementPermId, -1, mPosition );
      }
   }


   if( (lLapCompleted != NULL ) && mMasterMode && (mLapCount<mNbLapForRace) )
   {
      switch( lLapCompleted->mType )
      {
         case MR_CheckPoint::eCheck1:
            mCheckPoint1 = TRUE;
            mCheckPoint2 = FALSE;
            break;

         case MR_CheckPoint::eCheck2:
            if( mCheckPoint1 )
            {
               mCheckPoint2 = TRUE;
            }
            break;

         case MR_CheckPoint::eFinishLine:
            if( mCheckPoint2 )
            {
               mCheckPoint1 = FALSE;
               mCheckPoint2 = FALSE;

               mLapCount++;
               mLastLapDuration     = pTime-mLastLapCompletion;
               mLastLapCompletion   = pTime;

               if( (mLastLapDuration < mBestLapDuration)||(mLapCount==1) )
               {
                 mBestLapDuration = mLastLapDuration;
               }

               QueueSound( (mLapCount == mNbLapForRace) ? eSoundFinish : eSoundLineCrossing, TRUE, TRUE );
            }
            break;
      }
   }
}

const MR_ContactEffectList* MR_MainCharacter::GetEffectList()
{
   mContactEffect.mWeight = eCharacterWeight[mHoverModel];
   mContactEffect.mXSpeed = mXSpeed*256;
   mContactEffect.mYSpeed = mYSpeed*256;
   mContactEffect.mZSpeed = 0;

   return &mContactEffectList;
}

const MR_ShapeInterface* MR_MainCharacter::GetReceivingContactEffectShape()
{
   mCollisionShape.mPosition = mPosition;
   return &mCollisionShape;
}

const MR_ShapeInterface* MR_MainCharacter::GetGivingContactEffectShape()
{
   mContactShape.mPosition = mPosition;
   return &mContactShape;
}



// MR_MainCharacter::Cylinder
MR_Int32 MR_MainCharacter::Cylinder::ZMin()const
{
   return mPosition.mZ;
}

MR_Int32 MR_MainCharacter::Cylinder::ZMax()const
{
   return mPosition.mZ+eCharacterHeight;
}

MR_Int32 MR_MainCharacter::Cylinder::AxisX()const
{
   return mPosition.mX;
}

MR_Int32 MR_MainCharacter::Cylinder::AxisY()const
{
   return mPosition.mY;
}

MR_Int32 MR_MainCharacter::Cylinder::RayLen()const
{
   return mRay;
}

MR_Angle MR_MainCharacter::GetCabinOrientation()const
{
   return mCabinOrientation;
}


double MR_MainCharacter::GetFuelLevel()const
{
   return mFuelLevel/eFuelCapacity;
}

BOOL MR_MainCharacter::IsOutOfControl()const
{
   return mOutOfControlDuration > 0;
}

MR_MainCharacter::eWeapon MR_MainCharacter::GetCurrentWeapon()const
{
   return mCurrentWeapon;
}

int MR_MainCharacter::GetMissileRefillLevel( int pNbLevel )const
{
   if( mAllowWeapons )
   {
      return (pNbLevel-1)*(eMissileRefillTime-mMissileRefillDuration)/eMissileRefillTime;
   }
   else
   {
      return 0;
   }

}

int MR_MainCharacter::GetMineCount()const
{
   return mMineList.Used();
}

int MR_MainCharacter::GetPowerUpCount()const
{
   return mPowerUpList.Used();
}

int MR_MainCharacter::GetPowerUpFraction( int pNbLevel )const
{
   int lReturnValue = 0;

   if( mPowerUpLeft > 0 )
   {
      lReturnValue = 1+((mPowerUpLeft-1)*pNbLevel/ePwrUpDuration);

      if( lReturnValue > pNbLevel )
      {
         lReturnValue = pNbLevel;
      }
   }
   return lReturnValue;
}



double MR_MainCharacter::GetAbsoluteSpeed()const
{
   double lReturnValue = sqrt( mXSpeed*mXSpeed + mYSpeed*mYSpeed )/(eSteadySpeed[0]*1.9);

   if( lReturnValue > 1.0 )
   {
      lReturnValue = 1.0;
   }

   return lReturnValue;
}

double MR_MainCharacter::GetDirectionalSpeed()const
{
   double lReturnValue = (mXSpeed*MR_Cos[ mCabinOrientation ] + mYSpeed*MR_Sin[ mCabinOrientation ])/(MR_TRIGO_FRACT*eSteadySpeed[0]*1.9);

   if( lReturnValue > 1.0 )
   {
      lReturnValue = 1.0;
   }
   else if( lReturnValue < -1.0 )
   {
      lReturnValue = -1.0;
   }

   return lReturnValue;
}

int MR_MainCharacter::GetLap()const
{
   return mLapCount;
}

int MR_MainCharacter::GetTotalLap()const
{
   return mNbLapForRace;
}


MR_SimulationTime  MR_MainCharacter::GetTotalTime()const
{
   return mLastLapCompletion;
}

MR_SimulationTime  MR_MainCharacter::GetBestLapDuration()const
{
   return mBestLapDuration;
}

MR_SimulationTime  MR_MainCharacter::GetLastLapDuration()const
{
   return mLastLapDuration;
}

MR_SimulationTime  MR_MainCharacter::GetLastLapCompletion()const
{
   return mLastLapCompletion;
}

BOOL MR_MainCharacter::HasFinish()const
{
   return( mLapCount >= mNbLapForRace );
}


int MR_MainCharacter::HitQueueCount()const
{
   return mLastHits.Used();
}

int MR_MainCharacter::GetHitQueue()
{
   int lReturnValue = mLastHits.GetHead();

   mLastHits.Remove();

   return lReturnValue;
}

// TrackMap.cpp
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

#include "TrackMap.h"



// Local prototypes


// Implementation

// class MR_MapSprite

BOOL MR_MapSprite::CreateMap( MR_Level* pLevel, int& pX0, int& pY0, int& pX1, int& pY1 )
{
   // Safety check: if the level has no rooms, use default dimensions
   if( pLevel->GetRoomCount() == 0 )
   {
      pX0 = 0;
      pX1 = 256;
      pY0 = 0;
      pY1 = 256;
      return TRUE;
   }

   mNbItem     = 1;
   mWidth      = 200;
   mItemHeight = 200;
   mTotalHeight = mItemHeight;

   mData = new MR_UInt8[mTotalHeight*mWidth ];

   ComputeMinMax( pLevel );

   DrawMap( pLevel );

   pX0 = mXMin;
   pX1 = mXMax;
   pY0 = mYMin;
   pY1 = mYMax;

   return TRUE;
}

void MR_MapSprite::ComputeMinMax( MR_Level* pLevel )
{
   int lRoomCount = pLevel->GetRoomCount();
   
   mXMin = 1000000;
   mXMax = -1000000;
   mYMin = 1000000;
   mYMax = -1000000;

   for( int lRoom = 0; lRoom<lRoomCount; lRoom++ )
   {
      MR_PolygonShape* lShape = pLevel->GetRoomShape( lRoom );

      mXMin = std::min( mXMin, lShape->XMin() );
      mXMax = std::max( mXMax, lShape->XMax() );
      mYMin = std::min( mYMin, lShape->YMin() );
      mYMax = std::max( mYMax, lShape->YMax() );
      delete lShape;
   }

   mXMax += 1;
   mYMax += 1;

   // Now adjust Vertiacl and Horizontal ratio
   int lWidth  = mXMax-mXMin;
   int lHeight = mYMax-mYMin;
   int lDiff   = lWidth-lHeight;

   if( lDiff >= 0 )
   {
      mYMax += lDiff/2;
      mYMin -= lDiff/2;
   }
   else
   {
      mXMax += -lDiff/2;
      mXMin -= -lDiff/2;
   }

   // Safety: Ensure we don't have zero or negative dimensions
   if( mXMax <= mXMin ) mXMax = mXMin + 1;
   if( mYMax <= mYMin ) mYMax = mYMin + 1;
}

void MR_MapSprite::DrawMap( MR_Level* pLevel )
{
   // Clr the map
   memset( mData, 0, mTotalHeight*mWidth );

   // Do a kind of ray tracing
   int lRoomCount = pLevel->GetRoomCount();

   MR_ObjectFromFactoryId lFinishLineId = { 1, 202 };

   for( int lRoom = 0; lRoom<lRoomCount; lRoom++ )
   {
      int lColor = 61;

      // Verify if it is not the FinishLineSection
      MR_FreeElementHandle  lCurrentElem = pLevel->GetFirstFreeElement( lRoom );
      

      while( lCurrentElem != NULL )
      {
         if( MR_Level::GetFreeElement( lCurrentElem )->GetTypeId() == lFinishLineId )
         {
            lColor = 11;
         }

         lCurrentElem = MR_Level::GetNextFreeElement( lCurrentElem );
      }

      MR_PolygonShape* lShape = pLevel->GetRoomShape( lRoom );
      
      // Safety check for polygon validity
      if( lShape == NULL )
         continue;

      // Prevent division by zero
      int lXDiff = mXMax - mXMin;
      int lYDiff = mYMax - mYMin;
      if( lXDiff <= 0 ) lXDiff = 1;
      if( lYDiff <= 0 ) lYDiff = 1;

      int lXStart = (lShape->XMin()-mXMin)*mWidth/lXDiff;
      int lXEnd   = (lShape->XMax()-mXMin)*mWidth/lXDiff;
      int lYStart = (lShape->YMin()-mYMin)*mItemHeight/lYDiff;
      int lYEnd   = (lShape->YMax()-mYMin)*mItemHeight/lYDiff;

      // Bounds check
      if( lXStart < 0 ) lXStart = 0;
      if( lXEnd >= mWidth ) lXEnd = mWidth - 1;
      if( lYStart < 0 ) lYStart = 0;
      if( lYEnd >= mItemHeight ) lYEnd = mItemHeight - 1;

      for( int lX = lXStart; lX <=lXEnd && lX < mWidth; lX++ )
      {
         for( int lY = lYStart; lY<=lYEnd && lY < mItemHeight; lY++ )
         {
            MR_2DCoordinate lPos;

            lPos.mX = (lX*lXDiff/mWidth)+mXMin;
            lPos.mY = (lY*lYDiff/mItemHeight)+mYMin;

            if( MR_GetPolygonInclusion( *lShape, lPos ) )
            {
               int lIndex = (mItemHeight-1-lY)*mWidth+lX;
               if( lIndex >= 0 && lIndex < mTotalHeight*mWidth )
                  mData[ lIndex ] = lColor;
            }
         }
      }
      delete lShape;
   }
}

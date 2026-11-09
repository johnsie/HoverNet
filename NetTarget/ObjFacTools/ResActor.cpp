// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-09.
// See repository history for authorship and detailed changes.
// ResActor.cpp
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
#include "ResActor.h"
#include <cmath>

#include "ResourceLib.h"
#include "../VideoServices/3DViewport.h"

// ResActor
//
MR_ResActor::MR_ResActor( int pResourceId )
{
   mResourceId = pResourceId;
   mNbSequence = 0;
   mSequenceList = NULL;
}

MR_ResActor::~MR_ResActor()
{
   delete [] mSequenceList;
}

int MR_ResActor::GetResourceId()const
{
   return mResourceId;
}

int MR_ResActor::GetSequenceCount()const
{
   return mNbSequence;
}

int MR_ResActor::GetFrameCount( int pSequence )const
{
   return mSequenceList[ pSequence ].mNbFrame;
}

void MR_ResActor::Serialize( CArchive& pArchive, MR_ResourceLib* pLib )
{

   if( pArchive.IsStoring() )
   {
      pArchive << mNbSequence;
   }
   else
   {
      delete []mSequenceList;
      mSequenceList = NULL;

      pArchive >> mNbSequence;

      if( mNbSequence != 0 )
      {
         mSequenceList = new Sequence[ mNbSequence ];
      }
   }

   for( int lCounter = 0; lCounter<mNbSequence; lCounter++ )
   {
      mSequenceList[ lCounter ].Serialize( pArchive, pLib );
   }

}

void MR_ResActor::GetBounds( MR_3DCoordinate& pMin, MR_3DCoordinate& pMax )const
{
   bool lAny = false;
   pMin = MR_3DCoordinate( 0, 0, 0 );
   pMax = MR_3DCoordinate( 0, 0, 0 );
   for( int lSeq = 0; lSeq < mNbSequence; lSeq++ )
   {
      for( int lFrame = 0; lFrame < mSequenceList[ lSeq ].mNbFrame; lFrame++ )
      {
         const Frame& lF = mSequenceList[ lSeq ].mFrameList[ lFrame ];
         for( int lComp = 0; lComp < lF.mNbComponent; lComp++ )
         {
            const Patch* lPatch = (const Patch*)lF.mComponentList[ lComp ];
            for( int lNode = 0; lNode < lPatch->mURes*lPatch->mVRes; lNode++ )
            {
               const MR_3DCoordinate& lV = lPatch->mVertexList[ lNode ];
               if( !lAny ) { pMin = lV; pMax = lV; lAny = true; continue; }
               if( lV.mX < pMin.mX ) pMin.mX = lV.mX;
               if( lV.mY < pMin.mY ) pMin.mY = lV.mY;
               if( lV.mZ < pMin.mZ ) pMin.mZ = lV.mZ;
               if( lV.mX > pMax.mX ) pMax.mX = lV.mX;
               if( lV.mY > pMax.mY ) pMax.mY = lV.mY;
               if( lV.mZ > pMax.mZ ) pMax.mZ = lV.mZ;
            }
         }
      }
   }
}

MR_ResActor* MR_ResActor::CreateVariant( int pLength, int pWidth, int pHeight, int pTaper, const std::function<const MR_ResBitmap*( const MR_ResBitmap* )>& pBitmap )const
{
   MR_3DCoordinate lMin, lMax;
   GetBounds( lMin, lMax );
   const double lHalfLength = ( lMax.mX - lMin.mX )/2.0 > 1.0 ? ( lMax.mX - lMin.mX )/2.0 : 1.0;
   const double lCentreX = ( lMax.mX + lMin.mX )/2.0;
   MR_ResActor* lCopy = new MR_ResActor( mResourceId );
   lCopy->mNbSequence = mNbSequence;
   lCopy->mSequenceList = mNbSequence > 0 ? new Sequence[ mNbSequence ] : NULL;
   for( int lSeq = 0; lSeq < mNbSequence; lSeq++ )
   {
      Sequence& lTo = lCopy->mSequenceList[ lSeq ];
      const Sequence& lFrom = mSequenceList[ lSeq ];
      lTo.mNbFrame = lFrom.mNbFrame;
      lTo.mFrameList = lFrom.mNbFrame > 0 ? new Frame[ lFrom.mNbFrame ] : NULL;
      for( int lFrame = 0; lFrame < lFrom.mNbFrame; lFrame++ )
      {
         Frame& lToFrame = lTo.mFrameList[ lFrame ];
         const Frame& lFromFrame = lFrom.mFrameList[ lFrame ];
         lToFrame.mNbComponent = lFromFrame.mNbComponent;
         lToFrame.mComponentList = lFromFrame.mNbComponent > 0 ? new ActorComponent*[ lFromFrame.mNbComponent ] : NULL;
         for( int lComp = 0; lComp < lFromFrame.mNbComponent; lComp++ )
         {
            const Patch* lSrc = (const Patch*)lFromFrame.mComponentList[ lComp ];
            Patch* lPatch = new Patch;
            lPatch->mURes = lSrc->mURes;
            lPatch->mVRes = lSrc->mVRes;
            lPatch->mBitmap = pBitmap ? pBitmap( lSrc->mBitmap ) : lSrc->mBitmap;
            const int lCount = lSrc->mURes*lSrc->mVRes;
            lPatch->mVertexList = new MR_3DCoordinate[ lCount ];
            for( int lNode = 0; lNode < lCount; lNode++ )
            {
               const MR_3DCoordinate& lV = lSrc->mVertexList[ lNode ];
               const double lAlong = ( lV.mX - lCentreX )/lHalfLength;                       // -1 at the back, +1 at the front
               const double lWidthScale = ( 1.0 + pWidth/100.0 )*( 1.0 + pTaper/100.0*lAlong );
               lPatch->mVertexList[ lNode ].mX = (MR_Int32)floor( lCentreX + ( lV.mX - lCentreX )*( 1.0 + pLength/100.0 ) + 0.5 );
               lPatch->mVertexList[ lNode ].mY = (MR_Int32)floor( lV.mY*lWidthScale + 0.5 );
               lPatch->mVertexList[ lNode ].mZ = (MR_Int32)floor( lMin.mZ + ( lV.mZ - lMin.mZ )*( 1.0 + pHeight/100.0 ) + 0.5 );
            }
            lToFrame.mComponentList[ lComp ] = lPatch;
         }
      }
   }
   return lCopy;
}

void MR_ResActor::Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pSequence, int pFrame )const
{
   ASSERT( pSequence < mNbSequence );

   mSequenceList[ pSequence ].Draw( pDest, pMatrix, pFrame );
}


// Sequence
MR_ResActor::Sequence::Sequence()
{
   mNbFrame = 0;
   mFrameList = NULL;
}

MR_ResActor::Sequence::~Sequence()
{
   delete []mFrameList;
}

void MR_ResActor::Sequence::Serialize( CArchive& pArchive, MR_ResourceLib* pLib )
{
   if( pArchive.IsStoring() )
   {
      pArchive << mNbFrame;
   }
   else
   {
      delete []mFrameList;
      mFrameList = NULL;

      pArchive >> mNbFrame;

      if( mNbFrame != 0 )
      {
         mFrameList = new Frame[ mNbFrame ];
      }
   }

   for( int lCounter = 0; lCounter<mNbFrame; lCounter++ )
   {
      mFrameList[ lCounter ].Serialize( pArchive, pLib );
   }
}

void MR_ResActor::Sequence::Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pFrame )const
{
   ASSERT( pFrame < mNbFrame );

   mFrameList[ pFrame ].Draw( pDest, pMatrix );
}

// Frame
//

MR_ResActor::Frame::Frame()
{
   mNbComponent   = 0;
   mComponentList = NULL;

}

MR_ResActor::Frame::~Frame()
{
   Clean();
}

void MR_ResActor::Frame::Clean()
{
   if( mComponentList != NULL )
   {
      for( int lCounter = 0; lCounter < mNbComponent; lCounter++ )
      {
         delete mComponentList[ lCounter ];
      }

      delete []mComponentList;
      mComponentList = NULL;
   }
}

void MR_ResActor::Frame::Serialize( CArchive& pArchive, MR_ResourceLib* pLib )
{
   int lCounter;

   if( pArchive.IsStoring() )
   {
      pArchive << mNbComponent;

      for( lCounter =0; lCounter < mNbComponent; lCounter++ )
      {
         pArchive << (int)mComponentList[ lCounter ]->GetType();
         mComponentList[ lCounter ]->Serialize( pArchive, pLib );
      }
   }
   else
   {
      Clean();

      pArchive >> mNbComponent;

      if( mNbComponent != 0 )
      {
         mComponentList = new ActorComponent*[ mNbComponent ];

         for( lCounter =0; lCounter < mNbComponent; lCounter++ )
         {
            int lType;

            pArchive >> lType;

            switch( (eComponentType) lType )
            {
               case ePatch:
                  mComponentList[ lCounter ] = new Patch;
                  break;

               default:
                  ASSERT( FALSE );
                  AfxThrowArchiveException( CArchiveException::badSchema );
            }

            mComponentList[ lCounter ]->Serialize( pArchive, pLib );
         }
      }
   }
}


void MR_ResActor::Frame::Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix )const
{
   // Draw each component of the frame
   for( int lCounter = 0; lCounter < mNbComponent; lCounter++ )
   {
      mComponentList[ lCounter ]->Draw( pDest, pMatrix );
   }
}


// class ActorComponent
MR_ResActor::ActorComponent::~ActorComponent()
{
   // Notting to do
}

// Patch
//
MR_ResActor::Patch::Patch()
{
   mBitmap     = NULL;
   mVertexList = NULL;
}

MR_ResActor::Patch::~Patch()
{
   delete []mVertexList;
}

MR_ResActor::eComponentType MR_ResActor::Patch::GetType()const
{
   return ePatch;
}

void MR_ResActor::Patch::Serialize( CArchive& pArchive, MR_ResourceLib* pLib )
{
   int lCounter;

   if( pArchive.IsStoring() )
   {
      pArchive << mURes;
      pArchive << mVRes;      
      pArchive << mBitmap->GetResourceId(); //bitmaptype is serialize using the Id of the bitmap

      for( lCounter =0; lCounter < mURes*mVRes; lCounter++ )
      {
         mVertexList[ lCounter ].Serialize( pArchive );
      }
   }
   else
   {
      int lBitmapId;

      delete [] mVertexList;      

      pArchive >> mURes;
      pArchive >> mVRes;      
      pArchive >> lBitmapId;
      
      mBitmap = pLib->GetBitmap( lBitmapId );
      
      mVertexList = new MR_3DCoordinate[ mURes*mVRes ];

      for( lCounter =0; lCounter < mURes*mVRes; lCounter++ )
      {
         mVertexList[ lCounter ].Serialize( pArchive );
      }
   }
}

void MR_ResActor::Patch::Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix )const
{
   pDest->RenderPatch( *this, pMatrix , mBitmap );
}

int MR_ResActor::Patch::GetURes()const
{
   return mURes;
}

int MR_ResActor::Patch::GetVRes()const
{
   return mVRes;
}

const MR_3DCoordinate* MR_ResActor::Patch::GetNodeList( )const
{
   return mVertexList;
}



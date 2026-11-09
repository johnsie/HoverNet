// 3DViewportScene.cpp -- the viewport's Render* services as triangles for a scene sink (see MR_SceneSink in 3DViewport.h).
//
// Everything is worked out with the rasteriser's own transforms (ApplyRotationMatrix, ApplyPositionMatrix) so the camera is exactly the one the software
// path uses; here the surfaces are only clipped against the projection plane and cut into triangles. Nothing here moves the camera or changes where anything is.
#include "StdAfx.h"

#include "3DViewport.h"

#include <vector>

namespace
{
typedef std::vector<MR_SceneVertex> ScenePolygon;

// Clips a convex polygon against the projection plane (x >= pNear), keeping the texture coordinates in step.
void ClipNear( const ScenePolygon& pIn, float pNear, ScenePolygon& pOut )
{
   pOut.clear();
   const size_t lCount = pIn.size();
   for( size_t lIndex = 0; lIndex < lCount; lIndex++ )
   {
      const MR_SceneVertex& lA = pIn[ lIndex ];
      const MR_SceneVertex& lB = pIn[ (lIndex + 1) % lCount ];
      const bool lAIn = lA.x >= pNear, lBIn = lB.x >= pNear;
      if( lAIn )
      {
         pOut.push_back( lA );
      }
      if( lAIn != lBIn )
      {
         const float lT = (pNear - lA.x)/(lB.x - lA.x);
         MR_SceneVertex lCut;
         lCut.x = pNear;
         lCut.y = lA.y + (lB.y - lA.y)*lT;
         lCut.z = lA.z + (lB.z - lA.z)*lT;
         lCut.u = lA.u + (lB.u - lA.u)*lT;
         lCut.v = lA.v + (lB.v - lA.v)*lT;
         pOut.push_back( lCut );
      }
   }
}

void Fan( const ScenePolygon& pPolygon, std::vector<MR_SceneVertex>& pTriangles )
{
   for( size_t lIndex = 1; lIndex + 1 < pPolygon.size(); lIndex++ )
   {
      pTriangles.push_back( pPolygon[ 0 ] );
      pTriangles.push_back( pPolygon[ lIndex ] );
      pTriangles.push_back( pPolygon[ lIndex + 1 ] );
   }
}

MR_SceneVertex Vertex( const MR_3DCoordinate& pCamera, float pU, float pV )
{
   MR_SceneVertex lVertex;
   lVertex.x = (float)pCamera.mX; lVertex.y = (float)pCamera.mY; lVertex.z = (float)pCamera.mZ; lVertex.u = pU; lVertex.v = pV;
   return lVertex;
}
}

void MR_3DViewPort::EmitWall( const MR_3DCoordinate& pUpperLeft, const MR_3DCoordinate& pLowerRight, MR_Int32 pLen, const MR_Bitmap* pBitmap )
{
   if( pBitmap == NULL || pLen <= 0 || pBitmap->GetWidth() <= 0 || pBitmap->GetHeight() <= 0 )
   {
      return;
   }
   // the wall is seen from one side only, as in the software path
   if( Int32x32To64( pLowerRight.mY-pUpperLeft.mY, pUpperLeft.mX-mPosition.mX ) + Int32x32To64( -pLowerRight.mX+pUpperLeft.mX, pUpperLeft.mY-mPosition.mY ) >= 0 )
   {
      return;
   }
   MR_3DCoordinate lRotated0, lRotated1;
   ApplyRotationMatrix( pUpperLeft, lRotated0 );
   ApplyRotationMatrix( pLowerRight, lRotated1 );
   if( lRotated0.mX < mPlanDist && lRotated1.mX < mPlanDist )
   {
      return;
   }
   const float lU1 = (float)pLen/(float)pBitmap->GetWidth();
   const float lV1 = (float)(pUpperLeft.mZ - pLowerRight.mZ)/(float)pBitmap->GetHeight();
   if( lV1 == 0.0f )
   {
      return;
   }
   MR_3DCoordinate lTopA = lRotated0, lTopB = lRotated1, lBottomB = lRotated1, lBottomA = lRotated0;
   lTopB.mZ = lRotated0.mZ; lBottomB.mZ = lRotated1.mZ; lBottomA.mZ = lRotated1.mZ;
   ScenePolygon lQuad, lClipped;
   lQuad.push_back( Vertex( lTopA, 0.0f, 0.0f ) );
   lQuad.push_back( Vertex( lTopB, lU1, 0.0f ) );
   lQuad.push_back( Vertex( lBottomB, lU1, lV1 ) );
   lQuad.push_back( Vertex( lBottomA, 0.0f, lV1 ) );
   ClipNear( lQuad, (float)mPlanDist, lClipped );
   std::vector<MR_SceneVertex> lTriangles;
   Fan( lClipped, lTriangles );
   if( !lTriangles.empty() )
   {
      mSink->Triangles( &lTriangles[ 0 ], (int)lTriangles.size(), pBitmap, 0, mSurfaceTag );
   }
}

void MR_3DViewPort::EmitHorizontal( int pNbVertex, const MR_2DCoordinate* pVertexList, MR_Int32 pLevel, BOOL pTop, const MR_Bitmap* pBitmap )
{
   if( pBitmap == NULL || pNbVertex < 3 || pBitmap->GetWidth() <= 0 || pBitmap->GetHeight() <= 0 )
   {
      return;
   }
   const MR_Int32 lLevel = pLevel - mPosition.mZ;
   if( !( (pTop && lLevel >= 0) || (!pTop && lLevel <= 0) ) )
   {
      return;   // the far side of the surface
   }
   ScenePolygon lPolygon, lClipped;
   for( int lIndex = 0; lIndex < pNbVertex; lIndex++ )
   {
      MR_2DCoordinate lRotated;
      ApplyRotationMatrix( pVertexList[ lIndex ], lRotated );
      MR_SceneVertex lVertex;
      lVertex.x = (float)lRotated.mX; lVertex.y = (float)lRotated.mY; lVertex.z = (float)lLevel;
      lVertex.u = (float)pVertexList[ lIndex ].mX/(float)pBitmap->GetWidth();
      lVertex.v = -(float)pVertexList[ lIndex ].mY/(float)pBitmap->GetHeight();
      lPolygon.push_back( lVertex );
   }
   ClipNear( lPolygon, (float)mPlanDist, lClipped );
   std::vector<MR_SceneVertex> lTriangles;
   Fan( lClipped, lTriangles );
   if( !lTriangles.empty() )
   {
      mSink->Triangles( &lTriangles[ 0 ], (int)lTriangles.size(), pBitmap, 0, mSurfaceTag );
   }
}

void MR_3DViewPort::EmitPatch( const MR_Patch& pPatch, const MR_PositionMatrix& pMatrix, const MR_Bitmap* pBitmap, MR_UInt8 pFlatIndex )
{
   const int lURes = pPatch.GetURes();
   const int lVRes = pPatch.GetVRes();
   if( lURes < 2 || lVRes < 2 )
   {
      return;
   }
   const MR_3DCoordinate* lNodes = pPatch.GetNodeList();
   std::vector<MR_3DCoordinate> lCamera( (size_t)lURes*lVRes );
   for( int lNode = 0; lNode < lURes*lVRes; lNode++ )
   {
      ApplyPositionMatrix( pMatrix, lNodes[ lNode ], lCamera[ lNode ] );
   }
   std::vector<MR_SceneVertex> lTriangles;
   ScenePolygon lTriangle, lClipped;
   for( int lV = 0; lV < lVRes - 1; lV++ )
   {
      for( int lU = 0; lU < lURes - 1; lU++ )
      {
         // the cell's four nodes (the software path steps U along a row and V down the rows), as two triangles
         const int lA = lV*lURes + lU, lB = lA + 1, lC = lA + lURes, lD = lC + 1;
         const float lU0 = (float)lU/(float)(lURes-1), lU1 = (float)(lU+1)/(float)(lURes-1), lV0 = (float)lV/(float)(lVRes-1), lV1 = (float)(lV+1)/(float)(lVRes-1);
         const MR_SceneVertex lVa = Vertex( lCamera[ lA ], lU0, lV0 ), lVb = Vertex( lCamera[ lB ], lU1, lV0 ), lVc = Vertex( lCamera[ lC ], lU0, lV1 ), lVd = Vertex( lCamera[ lD ], lU1, lV1 );
         const MR_SceneVertex* lTris[ 2 ][ 3 ] = { { &lVa, &lVb, &lVc }, { &lVb, &lVd, &lVc } };
         for( int lT = 0; lT < 2; lT++ )
         {
            lTriangle.assign( 3, *lTris[ lT ][ 0 ] );
            lTriangle[ 1 ] = *lTris[ lT ][ 1 ];
            lTriangle[ 2 ] = *lTris[ lT ][ 2 ];
            if( lTriangle[ 0 ].x >= (float)mPlanDist && lTriangle[ 1 ].x >= (float)mPlanDist && lTriangle[ 2 ].x >= (float)mPlanDist )
            {
               lTriangles.push_back( lTriangle[ 0 ] ); lTriangles.push_back( lTriangle[ 1 ] ); lTriangles.push_back( lTriangle[ 2 ] );
            }
            else
            {
               ClipNear( lTriangle, (float)mPlanDist, lClipped );
               Fan( lClipped, lTriangles );
            }
         }
      }
   }
   if( !lTriangles.empty() )
   {
      mSink->Triangles( &lTriangles[ 0 ], (int)lTriangles.size(), pBitmap, pFlatIndex, mSurfaceTag );
   }
}

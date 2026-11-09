// ResActor.h
//


#ifndef MR_RES_ACTOR_H
#define MR_RES_ACTOR_H

#include "ResBitmap.h"
#include "../VideoServices/Patch.h"

#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_OBJ_FAC_TOOLS
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

#include <functional>

class MR_ResourceLib;
class MR_3DViewPort;
class MR_PositionMatrix;


class MR_ResActor
{
   public:
      friend class MR_ResActorFriend;

      enum eComponentType { ePatch };

   protected:

      class MR_DllDeclare ActorComponent
      {
      public:

         virtual ~ActorComponent();
         virtual eComponentType GetType()const = 0;
         virtual void           Serialize( CArchive& pArchive, MR_ResourceLib* pLib ) = 0;
         virtual void           Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix )const = 0;

      };

      class MR_DllDeclare Patch: public ActorComponent, public MR_Patch
      {
      public:
         int                 mURes;
         int                 mVRes;
         const MR_ResBitmap* mBitmap;
         MR_3DCoordinate*    mVertexList;

         Patch();
         ~Patch();

         eComponentType GetType()const;
         void           Serialize( CArchive& pArchive, MR_ResourceLib* pLib );
         void           Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix )const;

         int   GetURes()const;
         int   GetVRes()const;
         const MR_3DCoordinate* GetNodeList( )const;          

      };


      class MR_DllDeclare Frame
      {
      public:
         int              mNbComponent;
         ActorComponent** mComponentList;

         Frame();
         ~Frame();
         void Clean();
         void Serialize( CArchive& pArchive, MR_ResourceLib* pLib );
         void Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix )const;

      };

      class MR_DllDeclare Sequence
      {
      public:
         int     mNbFrame;
         Frame*  mFrameList;

         Sequence();
         ~Sequence();
         void Serialize( CArchive& pArchive, MR_ResourceLib* pLib );
         void Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pFrame )const;

      };


      int       mResourceId;
      int       mNbSequence;
      Sequence* mSequenceList;

   public:
      MR_DllDeclare MR_ResActor( int mResourceId );   // Only availlable for resourceLib and construction
      MR_DllDeclare ~MR_ResActor();

      MR_DllDeclare int  GetResourceId()const;

      MR_DllDeclare int  GetSequenceCount()const;
      MR_DllDeclare int  GetFrameCount( int pSequence )const;


      MR_DllDeclare void Serialize( CArchive& pArchive, MR_ResourceLib* pLib=NULL );
      MR_DllDeclare void Draw( MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pSequence, int pFrame )const;

      // A reshaped copy for a player's craft design (a look only: the simulation never sees it). Percent changes of the length (x, the way the craft points),
      // width (y) and height (z, about the craft's lowest point); pTaper widens (+) or narrows (-) the front against the back. pBitmap, when set, gives the
      // bitmap each patch uses (the copy does not own what it returns). The bounds used are those of the whole actor, so every frame is scaled alike.
      MR_DllDeclare MR_ResActor* CreateVariant( int pLength, int pWidth, int pHeight, int pTaper, const std::function<const MR_ResBitmap*( const MR_ResBitmap* )>& pBitmap )const;
      MR_DllDeclare void GetBounds( MR_3DCoordinate& pMin, MR_3DCoordinate& pMax )const;

};



#undef MR_DllDeclare

#endif





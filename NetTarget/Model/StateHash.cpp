#include "StdAfx.h"

#include "StateHash.h"
#include "Level.h"

std::uint64_t MR_HashLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime )
{
   MR_StateHasher lHasher;
   lHasher.Int( pSimulationTime );

   int lCount = 0;
   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
      {
         const MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
         lHasher.Int( lRoom );
         lHasher.Int( lElement->GetTypeId().mDllId );
         lHasher.Int( lElement->GetTypeId().mClassId );
         lHasher.Int( lElement->mPosition.mX );
         lHasher.Int( lElement->mPosition.mY );
         lHasher.Int( lElement->mPosition.mZ );
         lHasher.Int( lElement->mOrientation );
         lElement->HashLogicState( lHasher );
         ++lCount;
      }
   }
   lHasher.Int( lCount );
   return lHasher.Value();
}

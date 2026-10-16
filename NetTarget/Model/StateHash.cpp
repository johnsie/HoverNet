#include "StdAfx.h"

#include "StateHash.h"
#include "Level.h"

std::uint64_t MR_HashLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime )
{
   MR_StateHasher lHasher;
   std::int64_t lTime = pSimulationTime;
   lHasher.Int( lTime );

   int lCount = 0;
   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
      {
         MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
         int lRoomValue = lRoom;
         MR_UInt16 lDll = lElement->GetTypeId().mDllId, lClass = lElement->GetTypeId().mClassId;
         lHasher.Int( lRoomValue );
         lHasher.Int( lDll );
         lHasher.Int( lClass );
         lHasher.Int( lElement->mPosition.mX );
         lHasher.Int( lElement->mPosition.mY );
         lHasher.Int( lElement->mPosition.mZ );
         lHasher.Int( lElement->mOrientation );
         lElement->VisitLogicState( lHasher );
         ++lCount;
      }
   }
   lHasher.Int( lCount );
   return lHasher.Value();
}

void MR_DumpLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime, std::FILE* pOut )
{
   std::fprintf( pOut, "time = %lld\n", static_cast<long long>( pSimulationTime ) );
   MR_StateDumper lDumper( pOut );
   int lIndex = 0;
   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
      {
         MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
         std::fprintf( pOut, "element %d: room %d, class %d/%d, pos (%d,%d,%d), orientation %d\n", lIndex++, lRoom,
                       (int)lElement->GetTypeId().mDllId, (int)lElement->GetTypeId().mClassId, (int)lElement->mPosition.mX,
                       (int)lElement->mPosition.mY, (int)lElement->mPosition.mZ, (int)lElement->mOrientation );
         lDumper.StartElement();
         lElement->VisitLogicState( lDumper );
      }
   }
}

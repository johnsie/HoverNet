#include "StdAfx.h"

#include <algorithm>
#include <map>

#include "StateHash.h"
#include "StateSnapshot.h"
#include "Level.h"
#include "../Util/DllObjectFactory.h"

namespace
{
const std::int64_t kMagic = 0x53534E48;       // "HNSS"
const std::int64_t kMaxElements = 100000;
const std::int64_t kKindPermanent = 0;
const std::int64_t kKindExternal = 1;
const std::int64_t kKindTransient = 2;

std::uint64_t Checksum( const std::uint8_t* pData, std::size_t pSize )
{
   std::uint64_t lHash = 1469598103934665603ULL;
   for( std::size_t lIndex = 0; lIndex < pSize; ++lIndex )
   {
      lHash ^= pData[ lIndex ];
      lHash *= 1099511628211ULL;
   }
   return lHash;
}

struct Classification
{
   std::int64_t mKind;
   std::int64_t mKey;
};

Classification Classify( const MR_Level* pLevel, MR_FreeElement* pElement, const std::vector<MR_FreeElement*>& pExternal )
{
   for( std::size_t lIndex = 0; lIndex < pExternal.size(); ++lIndex )
   {
      if( pExternal[ lIndex ] == pElement ) return Classification{ kKindExternal, static_cast<std::int64_t>( lIndex ) };
   }
   for( int lIndex = 0; lIndex < pLevel->GetPermanentElementCount(); ++lIndex )
   {
      if( MR_Level::GetFreeElement( pLevel->GetPermanentElementHandle( lIndex ) ) == pElement )
      {
         return Classification{ kKindPermanent, lIndex };
      }
   }
   return Classification{ kKindTransient, 0 };
}
}

std::vector<std::uint8_t> MR_SaveLevelState( const MR_Level* pLevel, std::int64_t pSimulationTime,
                                             const std::vector<MR_FreeElement*>& pExternal, std::uint32_t pRules )
{
   MR_StateWriter lWriter;
   std::int64_t lMagic = kMagic, lVersion = kSnapshotVersion, lRules = pRules, lTime = pSimulationTime;
   lWriter.Int( lMagic );
   lWriter.Int( lVersion );
   lWriter.Int( lRules );
   lWriter.Int( lTime );

   std::int64_t lCount = 0;
   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) ) ++lCount;
   }
   lWriter.Int( lCount );

   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
      {
         MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
         Classification lClass = Classify( pLevel, lElement, pExternal );
         std::int64_t lRoomValue = lRoom, lDll = lElement->GetTypeId().mDllId, lClassId = lElement->GetTypeId().mClassId;
         lWriter.Int( lClass.mKind );
         lWriter.Int( lClass.mKey );
         lWriter.Int( lRoomValue );
         lWriter.Int( lDll );
         lWriter.Int( lClassId );
         lWriter.Int( lElement->mPosition.mX );
         lWriter.Int( lElement->mPosition.mY );
         lWriter.Int( lElement->mPosition.mZ );
         lWriter.Int( lElement->mOrientation );
         lElement->VisitLogicState( lWriter );
      }
   }

   // Trailer: a checksum of everything before it.
   std::int64_t lSum = static_cast<std::int64_t>( Checksum( lWriter.mBytes.data(), lWriter.mBytes.size() ) );
   lWriter.Int( lSum );
   return lWriter.mBytes;
}

static bool RestoreLevelStateUnchecked( MR_Level* pLevel, const std::uint8_t* pData, std::size_t pSize,
                           const std::vector<MR_FreeElement*>& pExternal, std::uint32_t pRules,
                           std::int64_t& pSimulationTime, std::string& pError )
{
   if( pSize < 8 * 6 )
   {
      pError = "snapshot too short";
      return false;
   }
   {
      MR_StateReader lTail( pData + pSize - 8, 8 );
      std::int64_t lSum = 0;
      lTail.Int( lSum );
      if( static_cast<std::uint64_t>( lSum ) != Checksum( pData, pSize - 8 ) )
      {
         pError = "checksum mismatch (damaged snapshot)";
         return false;
      }
   }

   MR_StateReader lReader( pData, pSize - 8 );
   std::int64_t lMagic = 0, lVersion = 0, lRules = 0, lTime = 0, lCount = 0;
   lReader.Int( lMagic );
   lReader.Int( lVersion );
   lReader.Int( lRules );
   lReader.Int( lTime );
   lReader.Int( lCount );
   if( !lReader.Ok() || lMagic != kMagic ) { pError = "not a HoverNet snapshot"; return false; }
   if( lVersion != kSnapshotVersion ) { pError = "unsupported snapshot version"; return false; }
   if( lRules != static_cast<std::int64_t>( pRules ) ) { pError = "snapshot is from another simulation rules version"; return false; }
   if( lCount < 0 || lCount > kMaxElements ) { pError = "bad element count"; return false; }

   // Index what the level already has: permanent elements by number, external ones from the caller.
   std::map<std::int64_t, MR_FreeElementHandle> lPermanent;
   for( int lIndex = 0; lIndex < pLevel->GetPermanentElementCount(); ++lIndex ) lPermanent[ lIndex ] = pLevel->GetPermanentElementHandle( lIndex );
   std::map<MR_FreeElement*, MR_FreeElementHandle> lExternalHandle;

   // Remove the transient elements; remember where the external ones are.
   std::vector<MR_FreeElementHandle> lToDelete;
   for( int lRoom = -1; lRoom < pLevel->GetRoomCount(); ++lRoom )
   {
      for( MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
      {
         MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
         Classification lClass = Classify( pLevel, lElement, pExternal );
         if( lClass.mKind == kKindTransient ) lToDelete.push_back( lHandle );
         else if( lClass.mKind == kKindExternal ) lExternalHandle[ lElement ] = lHandle;
      }
   }
   // Deleting unlinks; collect first, delete after the walk (done below).
   for( std::size_t lIndex = 0; lIndex < lToDelete.size(); ++lIndex )
   {
      MR_Level::DeleteElement( lToDelete[ lIndex ] );   // also destroys the element
   }

   struct Placed { MR_FreeElementHandle mHandle; int mRoom; };
   std::vector<Placed> lPlaced;
   lPlaced.reserve( static_cast<std::size_t>( lCount ) );

   for( std::int64_t lItem = 0; lItem < lCount; ++lItem )
   {
      std::int64_t lKind = 0, lKey = 0, lRoom = 0, lDll = 0, lClassId = 0;
      lReader.Int( lKind );
      lReader.Int( lKey );
      lReader.Int( lRoom );
      lReader.Int( lDll );
      lReader.Int( lClassId );
      if( !lReader.Ok() ) { pError = "truncated snapshot"; return false; }
      if( lRoom < -1 || lRoom >= pLevel->GetRoomCount() ) { pError = "element in a room the level does not have"; return false; }

      MR_FreeElementHandle lHandle = NULL;
      MR_FreeElement* lElement = NULL;
      if( lKind == kKindPermanent )
      {
         std::map<std::int64_t, MR_FreeElementHandle>::iterator lFound = lPermanent.find( lKey );
         if( lFound == lPermanent.end() ) { pError = "snapshot is for a different track (permanent element)"; return false; }
         lHandle = lFound->second;
         lElement = MR_Level::GetFreeElement( lHandle );
      }
      else if( lKind == kKindExternal )
      {
         if( lKey < 0 || lKey >= static_cast<std::int64_t>( pExternal.size() ) ) { pError = "snapshot has more program-owned elements than the level"; return false; }
         lElement = pExternal[ static_cast<std::size_t>( lKey ) ];
         std::map<MR_FreeElement*, MR_FreeElementHandle>::iterator lFound = lExternalHandle.find( lElement );
         if( lFound == lExternalHandle.end() ) { pError = "a program-owned element is not in the level"; return false; }
         lHandle = lFound->second;
      }
      else if( lKind == kKindTransient )
      {
         MR_ObjectFromFactoryId lId = { static_cast<MR_UInt16>( lDll ), static_cast<MR_UInt16>( lClassId ) };
         MR_ObjectFromFactory* lObject = MR_DllObjectFactory::CreateObject( lId );
         lElement = dynamic_cast<MR_FreeElement*>( lObject );
         if( lElement == NULL ) { delete lObject; pError = "snapshot has an element the factory cannot create"; return false; }
         lHandle = pLevel->InsertElement( lElement, static_cast<int>( lRoom ), FALSE );
      }
      else
      {
         pError = "bad element kind";
         return false;
      }
      if( lElement->GetTypeId().mDllId != lDll || lElement->GetTypeId().mClassId != lClassId ) { pError = "element type does not match the level"; return false; }

      lReader.Int( lElement->mPosition.mX );
      lReader.Int( lElement->mPosition.mY );
      lReader.Int( lElement->mPosition.mZ );
      lReader.Int( lElement->mOrientation );
      lElement->VisitLogicState( lReader );
      if( !lReader.Ok() ) { pError = "truncated snapshot"; return false; }
      lPlaced.push_back( Placed{ lHandle, static_cast<int>( lRoom ) } );
   }
   if( lReader.Remaining() != 0 ) { pError = "trailing data"; return false; }

   // Rebuild each room's list order: linking puts an element at the head, so link in reverse.
   for( std::size_t lIndex = lPlaced.size(); lIndex > 0; --lIndex )
   {
      pLevel->MoveElement( lPlaced[ lIndex - 1 ].mHandle, lPlaced[ lIndex - 1 ].mRoom );
   }
   pSimulationTime = lTime;
   return true;
}

// Hostile or damaged data must never take the program down: anything the object factory or an element throws is a refusal.
bool MR_RestoreLevelState( MR_Level* pLevel, const std::uint8_t* pData, std::size_t pSize,
                           const std::vector<MR_FreeElement*>& pExternal, std::uint32_t pRules,
                           std::int64_t& pSimulationTime, std::string& pError )
{
   try
   {
      return RestoreLevelStateUnchecked( pLevel, pData, pSize, pExternal, pRules, pSimulationTime, pError );
   }
   catch( ... )
   {
      pError = "the snapshot describes something the simulation cannot create";
      return false;
   }
}

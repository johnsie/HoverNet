#include "RaceWorld.h"

#include "ReplayFormat.h"
#include "../MainCharacter/StdAfx.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Model/GameSession.h"
#include "../Model/StateHash.h"
#include "../Model/StateSnapshot.h"
#include "../Util/RecordFile.h"

namespace Authority
{
RaceWorld::RaceWorld() : mTick( 0 ) {}
RaceWorld::~RaceWorld() {}

bool RaceWorld::Build( const std::string& trackPath, const std::string& trackName, int players, int laps, bool weapons, std::string& error, std::int64_t startTimeMs )
{
    mStartMs = startTimeMs;
    MR_RecordFile* lTrack = new MR_RecordFile;
    if( !lTrack->OpenForRead( trackPath.c_str() ) ) { delete lTrack; error = "cannot open the track"; return false; }
    mSession.reset( new MR_GameSession( FALSE ) );
    if( !mSession->LoadNew( trackName.c_str(), lTrack ) || mSession->GetCurrentLevel() == NULL ) { error = "cannot load the track"; return false; }
    MR_Level* lLevel = mSession->GetCurrentLevel();
    if( players < 1 || players > ReplayFormat::kMaxPlayers || lLevel->GetPlayerCount() < players ) { error = "the track has fewer start positions than players"; return false; }
    mCraft.clear();
    for( int lPlayer = 0; lPlayer < players; ++lPlayer )
    {
        const int lRoom = lLevel->GetStartingRoom( lPlayer );
        MR_MainCharacter* lCraft = MR_MainCharacter::New( laps, weapons ? TRUE : FALSE );
        if( lCraft == NULL || lRoom < 0 || lRoom >= lLevel->GetRoomCount() ) { error = "cannot create a craft"; return false; }
        lCraft->mPosition = lLevel->GetStartingPos( lPlayer );
        lCraft->SetOrientation( lLevel->GetStartingOrientation( lPlayer ) );
        lCraft->mRoom = lRoom;
        lCraft->SetHoverId( lPlayer );   // hover id = grid slot: missiles and hits are attributed to it
        if( lLevel->InsertElement( lCraft, lRoom, FALSE ) == NULL ) { error = "cannot place a craft"; return false; }
        mCraft.push_back( lCraft );
    }
    mSession->SetSimulationTime( static_cast<MR_SimulationTime>( mStartMs ) );
    mTick = 0;
    return true;
}

void RaceWorld::Step( const std::uint16_t* controls )
{
    for( std::size_t lPlayer = 0; lPlayer < mCraft.size(); ++lPlayer ) mCraft[ lPlayer ]->SetControlState( controls[ lPlayer ], mSession->GetSimulationTime() );
    mSession->SimulateStep( ReplayFormat::kFrameMs );
    ++mTick;
}

std::uint64_t RaceWorld::Hash() const
{
    return MR_HashLevelState( mSession->GetCurrentLevel(), mSession->GetSimulationTime() );
}

std::vector<std::uint8_t> RaceWorld::Snapshot() const
{
    std::vector<MR_FreeElement*> lExternal( mCraft.begin(), mCraft.end() );
    return MR_SaveLevelState( mSession->GetCurrentLevel(), mSession->GetSimulationTime(), lExternal, ReplayFormat::kSimulationRules );
}

bool RaceWorld::Restore( const std::vector<std::uint8_t>& bytes, std::string& error )
{
    std::vector<MR_FreeElement*> lExternal( mCraft.begin(), mCraft.end() );
    std::int64_t lTime = 0;
    if( !MR_RestoreLevelState( mSession->GetCurrentLevel(), bytes.data(), bytes.size(), lExternal, ReplayFormat::kSimulationRules, lTime, error ) ) return false;
    mSession->SetSimulationTime( static_cast<MR_SimulationTime>( lTime ) );
    mTick = static_cast<std::uint32_t>( ( lTime - mStartMs ) / ReplayFormat::kFrameMs );
    return true;
}
}

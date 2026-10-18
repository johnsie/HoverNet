#include "AuthoritativeRace.h"

#include <algorithm>
#include <map>

#include "ReplayFormat.h"
#include "../LinuxClient/Sha256.h"
#include "../MainCharacter/StdAfx.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Model/GameSession.h"
#include "../Model/StateHash.h"
#include "../Model/StateSnapshot.h"
#include "../Util/RecordFile.h"

namespace Authority
{
AuthoritativeRace::AuthoritativeRace() : mTick( 0 ), mFinished( false ), mFreePlay( false ) {}
AuthoritativeRace::~AuthoritativeRace() {}

bool AuthoritativeRace::Start( const RaceConfig& config, std::string& error )
{
    if( config.players < 1 || config.players > ReplayFormat::kMaxPlayers ) { error = "bad player count"; return false; }
    if( config.laps < 1 || config.laps > 255 ) { error = "bad lap count"; return false; }
    mConfig = config;
    // Starting again (a race that starts with fewer players than it had seats) begins from nothing.
    mCraft.clear();
    mPending.clear();
    mLastInput.clear();
    mHistory.clear();
    mCheckpoints.clear();
    mEvents.clear();
    mLastLap.clear();
    mTick = 0;
    mFinished = false;

    MR_RecordFile* lTrack = new MR_RecordFile;
    if( !lTrack->OpenForRead( config.trackPath.c_str() ) )
    {
        delete lTrack;
        error = "cannot open the track";
        return false;
    }
    mTrackSha256 = Sha256::HashFile( config.trackPath );
    mSession.reset( new MR_GameSession( FALSE ) );
    if( !mSession->LoadNew( config.trackName.c_str(), lTrack ) || mSession->GetCurrentLevel() == NULL )
    {
        error = "cannot load the track";
        return false;
    }

    MR_Level* lLevel = mSession->GetCurrentLevel();
    if( lLevel->GetPlayerCount() < config.players ) { error = "the track has fewer start positions than players"; return false; }
    for( int lPlayer = 0; lPlayer < config.players; ++lPlayer )
    {
        const int lRoom = lLevel->GetStartingRoom( lPlayer );
        MR_MainCharacter* lCraft = MR_MainCharacter::New( config.laps, config.weapons ? TRUE : FALSE );
        if( lCraft == NULL || lRoom < 0 || lRoom >= lLevel->GetRoomCount() ) { error = "cannot create a craft"; return false; }
        lCraft->mPosition = lLevel->GetStartingPos( lPlayer );
        lCraft->SetOrientation( lLevel->GetStartingOrientation( lPlayer ) );
        lCraft->mRoom = lRoom;
        lCraft->SetHoverId( lPlayer );   // hover id = grid slot: missiles and hits are attributed to it
        if( lLevel->InsertElement( lCraft, lRoom, FALSE ) == NULL ) { error = "cannot place a craft"; return false; }
        mCraft.push_back( lCraft );
    }
    // A track without a finish line is free play: it has no laps to complete, so the race runs to a time limit (10 minutes unless
    // the configuration says otherwise) and the result ranks nobody by laps.
    mFreePlay = true;
    for( int lRoom = -1; lRoom < lLevel->GetRoomCount() && mFreePlay; ++lRoom )
    {
        for( MR_FreeElementHandle lHandle = lLevel->GetFirstFreeElement( lRoom ); lHandle != NULL; lHandle = MR_Level::GetNextFreeElement( lHandle ) )
        {
            const MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
            if( lElement->GetTypeId().mDllId == 1 && lElement->GetTypeId().mClassId == 202 ) { mFreePlay = false; break; }
        }
    }
    if( mFreePlay && mConfig.maxTicks == 0 ) mConfig.maxTicks = 37500;
    // The countdown is the clients' presentation phase; the authoritative race begins at tick 0 for everybody at once.
    mSession->SetSimulationTime( 0 );
    mLastInput.assign( config.players, 0 );
    mPending.assign( config.players, std::vector<std::int32_t>() );
    mTick = 0;
    mEvents.clear();
    mLastLap.clear();
    mFinished = false;
    return true;
}

// The pending inputs are kept in mPending[player] as a flat list of (tick, input) pairs encoded in one int64-free vector:
// pairs of values, tick then input, sorted by tick. It stays tiny (at most maxFutureTicks entries).
static void StoreInput( std::vector<std::int32_t>& pQueue, std::uint32_t pTick, std::uint16_t pInput, bool& pReplaced )
{
    pReplaced = false;
    for( std::size_t lIndex = 0; lIndex + 1 < pQueue.size(); lIndex += 2 )
    {
        if( static_cast<std::uint32_t>( pQueue[ lIndex ] ) == pTick )
        {
            pQueue[ lIndex + 1 ] = pInput;
            pReplaced = true;
            return;
        }
    }
    pQueue.push_back( static_cast<std::int32_t>( pTick ) );
    pQueue.push_back( pInput );
}

InputResult AuthoritativeRace::SubmitInput( int player, std::uint32_t tick, std::uint16_t controls )
{
    if( mFinished ) return InputResult::Finished;
    if( player < 0 || player >= mConfig.players ) return InputResult::BadPlayer;
    if( tick < mTick ) return InputResult::Late;
    if( tick > mTick + static_cast<std::uint32_t>( mConfig.maxFutureTicks ) ) return InputResult::TooFarAhead;

    bool lReplaced = false;
    StoreInput( mPending[ player ], tick, controls & kValidControlBits, lReplaced );
    return lReplaced ? InputResult::Replaced : InputResult::Accepted;
}

bool AuthoritativeRace::AdvanceTick()
{
    if( mFinished ) return false;

    for( int lPlayer = 0; lPlayer < mConfig.players; ++lPlayer )
    {
        std::vector<std::int32_t>& lQueue = mPending[ lPlayer ];
        std::vector<std::int32_t> lKept;
        for( std::size_t lIndex = 0; lIndex + 1 < lQueue.size(); lIndex += 2 )
        {
            const std::uint32_t lTick = static_cast<std::uint32_t>( lQueue[ lIndex ] );
            if( lTick == mTick ) mLastInput[ lPlayer ] = static_cast<std::uint16_t>( lQueue[ lIndex + 1 ] );
            else if( lTick > mTick )
            {
                lKept.push_back( lQueue[ lIndex ] );
                lKept.push_back( lQueue[ lIndex + 1 ] );
            }
        }
        lQueue.swap( lKept );
        mHistory.push_back( mLastInput[ lPlayer ] );
        mCraft[ lPlayer ]->SetControlState( mLastInput[ lPlayer ], mSession->GetSimulationTime() );
    }

    mSession->SimulateStep( ReplayFormat::kFrameMs );
    ++mTick;
    if( mTick % ReplayFormat::kCheckpointTicks == 0 ) mCheckpoints.push_back( StateHash() );

    if( mLastLap.size() != mCraft.size() ) mLastLap.assign( mCraft.size(), 0 );
    for( std::size_t lPlayer = 0; lPlayer < mCraft.size(); ++lPlayer )
    {
        const int lLap = mCraft[ lPlayer ]->GetLap();
        if( lLap > mLastLap[ lPlayer ] )
        {
            mLastLap[ lPlayer ] = lLap;
            RaceEvent lEvent;
            lEvent.tick = mTick;
            lEvent.player = static_cast<int>( lPlayer );
            lEvent.lap = lLap;
            lEvent.finished = mCraft[ lPlayer ]->HasFinish() != FALSE;
            lEvent.lapMs = mCraft[ lPlayer ]->GetLastLapDuration();
            lEvent.raceMs = mCraft[ lPlayer ]->GetTotalTime();
            mEvents.push_back( lEvent );
        }
    }

    bool lAllDone = true;
    for( std::size_t lPlayer = 0; lPlayer < mCraft.size(); ++lPlayer ) lAllDone = lAllDone && mCraft[ lPlayer ]->HasFinish();
    const std::uint32_t lLimit = ( mConfig.maxTicks > 0 && mConfig.maxTicks < ReplayFormat::kMaxTicks ) ? mConfig.maxTicks : ReplayFormat::kMaxTicks;
    if( lAllDone || mTick >= lLimit ) mFinished = true;
    return !mFinished;
}

std::uint64_t AuthoritativeRace::StateHash() const
{
    return MR_HashLevelState( mSession->GetCurrentLevel(), mSession->GetSimulationTime() );
}

std::vector<std::uint16_t> AuthoritativeRace::InputsAt( std::uint32_t tick ) const
{
    std::vector<std::uint16_t> lInputs;
    if( tick >= mTick ) return lInputs;
    const std::size_t lFirst = static_cast<std::size_t>( tick ) * mConfig.players;
    lInputs.assign( mHistory.begin() + static_cast<std::ptrdiff_t>( lFirst ), mHistory.begin() + static_cast<std::ptrdiff_t>( lFirst + mConfig.players ) );
    return lInputs;
}

std::vector<PlayerResult> AuthoritativeRace::Results() const
{
    std::vector<PlayerResult> lResults;
    for( std::size_t lPlayer = 0; lPlayer < mCraft.size(); ++lPlayer )
    {
        const MR_MainCharacter* lCraft = mCraft[ lPlayer ];
        PlayerResult lResult;
        lResult.player = static_cast<int>( lPlayer );
        lResult.finished = lCraft->HasFinish() != FALSE;
        lResult.laps = lCraft->GetLap();
        lResult.finishTimeMs = lCraft->GetTotalTime();
        lResult.bestLapMs = lCraft->GetBestLapDuration();
        lResults.push_back( lResult );
    }
    // Finishers by finishing time, then the others by laps completed (more first); ties keep grid order.
    std::vector<int> lOrder( lResults.size() );
    for( std::size_t lIndex = 0; lIndex < lOrder.size(); ++lIndex ) lOrder[ lIndex ] = static_cast<int>( lIndex );
    std::stable_sort( lOrder.begin(), lOrder.end(), [&lResults]( int pA, int pB ) {
        const PlayerResult& a = lResults[ pA ];
        const PlayerResult& b = lResults[ pB ];
        if( a.finished != b.finished ) return a.finished;
        if( a.finished ) return a.finishTimeMs < b.finishTimeMs;
        return a.laps > b.laps;
    } );
    for( std::size_t lRank = 0; lRank < lOrder.size(); ++lRank ) lResults[ lOrder[ lRank ] ].rank = static_cast<int>( lRank ) + 1;
    return lResults;
}

std::vector<std::uint8_t> AuthoritativeRace::ReplayBytes() const
{
    ReplayFormat::Replay lReplay;
    lReplay.track = mConfig.trackName;
    lReplay.trackSha256 = mTrackSha256;
    lReplay.ticks = mTick;
    lReplay.players = mConfig.players;
    lReplay.laps = mConfig.laps;
    lReplay.weapons = mConfig.weapons;
    lReplay.inputs = mHistory;
    lReplay.checkpoints = mCheckpoints;
    lReplay.finalHash = StateHash();
    return ReplayFormat::Encode( lReplay );
}

std::vector<std::uint8_t> AuthoritativeRace::Snapshot() const
{
    std::vector<MR_FreeElement*> lExternal( mCraft.begin(), mCraft.end() );
    return MR_SaveLevelState( mSession->GetCurrentLevel(), mSession->GetSimulationTime(), lExternal, ReplayFormat::kSimulationRules );
}

int AuthoritativeRace::CraftModel( int player ) const
{
    const MR_MainCharacter* lCraft = Craft( player );
    return lCraft == NULL ? -1 : lCraft->GetHoverModel();
}

const MR_MainCharacter* AuthoritativeRace::Craft( int player ) const
{
    return ( player >= 0 && player < static_cast<int>( mCraft.size() ) ) ? mCraft[ player ] : NULL;
}
}

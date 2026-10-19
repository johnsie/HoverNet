// AuthoritativeRace.h
//
// The server's side of a race in protocol 3.0 (docs/specs/protocol-3.0-draft.md): the server owns the simulation, takes
// each player's control word per tick, advances the race in exact 16 ms ticks, and is the only source of positions,
// laps, finishes and the result. A client can send inputs and nothing else; it cannot say where it is, what it hit or
// when it finished. The class has no sockets and no clock, so the same code serves the dedicated server, the tests and
// the replay tools.
#ifndef AUTHORITATIVE_RACE_H
#define AUTHORITATIVE_RACE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ReplayFormat.h"

class MR_GameSession;
class MR_MainCharacter;

namespace Authority
{
struct RaceConfig
{
    std::string trackName;      // as the game knows it (a bundled track name)
    std::string trackPath;      // the .trk file
    int players = 2;            // 1..8, player i starts on grid slot i
    int laps = 3;
    bool weapons = true;
    int maxFutureTicks = 64;    // an input for a tick further ahead than this is refused
    std::uint32_t maxTicks = 0; // time limit in ticks; 0 = the replay format's limit (two hours)
    std::uint32_t rules = ReplayFormat::kSimulationRules;   // the rules the race runs under (an older replay is re-simulated under its own: when the countdown starts)
};

enum class InputResult
{
    Accepted,       // stored for its tick
    Replaced,       // a second input for the same tick replaced the first
    Late,           // too late to be used: the server already simulated that tick without it
    TooFarAhead,    // for a tick beyond the allowed window
    BadPlayer,      // no such player
    Finished,       // the race is over
};

struct PlayerResult
{
    int player = 0;
    bool finished = false;
    int laps = 0;
    std::int64_t finishTimeMs = 0;   // race time of the last lap crossing
    std::int64_t bestLapMs = 0;
    int rank = 0;                     // 1 = winner; unfinished players rank by laps then are ordered after finishers
};

// A lap completed (the last one also finishes the race for that player). Recorded as the race runs; not part of the hashed state, so a
// race restored from a snapshot has only the events after it. A replay re-simulated from tick 0 yields the same events.
struct RaceEvent
{
    std::uint32_t tick = 0;           // the tick after which the lap count changed
    int player = 0;
    int lap = 0;                      // laps completed so far
    bool finished = false;
    std::int64_t lapMs = 0;           // duration of this lap
    std::int64_t raceMs = 0;          // race time at the crossing
};

class AuthoritativeRace
{
public:
    AuthoritativeRace();
    ~AuthoritativeRace();

    // Loads the track, puts the craft on the grid, and starts the countdown. Returns false and sets error on failure.
    bool Start(const RaceConfig& config, std::string& error);

    // An input for a given tick (the tick it should take effect on). Control bits the simulation does not define are
    // masked off; the player's previous input is repeated for any tick without one.
    InputResult SubmitInput(int player, std::uint32_t tick, std::uint16_t controls);

    // Simulates the next tick. Returns false once the race is over (every player has finished, or the time limit).
    bool AdvanceTick();

    std::uint32_t CurrentTick() const { return mTick; }           // ticks simulated so far
    bool Finished() const { return mFinished; }
    bool FreePlay() const { return mFreePlay; }                   // the track has no finish line: the race runs to a time limit
    std::uint64_t StateHash() const;                              // MR_HashLevelState of the whole race
    std::vector<PlayerResult> Results() const;
    const std::vector<RaceEvent>& Events() const { return mEvents; }

    // The control words every player's craft used at a tick that has been simulated (empty if not yet).
    std::vector<std::uint16_t> InputsAt(std::uint32_t tick) const;

    // The race so far as a verifiable replay file (docs/specs/replay-format.md) and as a state snapshot.
    std::vector<std::uint8_t> ReplayBytes() const;
    std::vector<std::uint8_t> Snapshot() const;

    // Reads a player's craft for display or tests only; the race itself is never changed through it.
    const MR_MainCharacter* Craft(int player) const;
    // The craft model a player drove (0 to 4, CraftNames.h), -1 if there is no such player. It is picked during the countdown and cannot change once the race is running.
    int CraftModel(int player) const;

    static constexpr std::uint16_t kValidControlBits = 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 2048;

private:
    RaceConfig mConfig;
    std::unique_ptr<MR_GameSession> mSession;
    std::vector<MR_MainCharacter*> mCraft;
    std::uint32_t mTick;
    bool mFinished;
    bool mFreePlay;
    std::vector<std::uint16_t> mLastInput;                           // per player
    std::vector<std::vector<std::int32_t>> mPending;                 // per player, ring of future inputs (-1 = none)
    std::vector<std::uint16_t> mHistory;                             // [tick * players + player], what was actually simulated
    std::vector<std::uint64_t> mCheckpoints;
    std::string mTrackSha256;
    std::vector<RaceEvent> mEvents;
    std::vector<int> mLastLap;                                        // per player, for detecting lap crossings
};
}

#endif

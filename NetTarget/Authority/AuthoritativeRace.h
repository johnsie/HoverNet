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
    std::uint64_t StateHash() const;                              // MR_HashLevelState of the whole race
    std::vector<PlayerResult> Results() const;

    // The control words every player's craft used at a tick that has been simulated (empty if not yet).
    std::vector<std::uint16_t> InputsAt(std::uint32_t tick) const;

    // The race so far as a verifiable replay file (docs/specs/replay-format.md) and as a state snapshot.
    std::vector<std::uint8_t> ReplayBytes() const;
    std::vector<std::uint8_t> Snapshot() const;

    // Reads a player's craft for display or tests only; the race itself is never changed through it.
    const MR_MainCharacter* Craft(int player) const;

    static constexpr std::uint16_t kValidControlBits = 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 2048;

private:
    RaceConfig mConfig;
    std::unique_ptr<MR_GameSession> mSession;
    std::vector<MR_MainCharacter*> mCraft;
    std::uint32_t mTick;
    bool mFinished;
    std::vector<std::uint16_t> mLastInput;                           // per player
    std::vector<std::vector<std::int32_t>> mPending;                 // per player, ring of future inputs (-1 = none)
    std::vector<std::uint16_t> mHistory;                             // [tick * players + player], what was actually simulated
    std::vector<std::uint64_t> mCheckpoints;
    std::string mTrackSha256;
};
}

#endif

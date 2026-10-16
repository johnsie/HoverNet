// PredictingClient.h -- the client's view of an authoritative race: exact state from the server, a prediction ahead of it.
//
// The *confirmed* world advances only with the server's Tick messages (the inputs every player used), so it is always exactly
// the server's race and every Tick's state hash is checked against it. The *predicted* world is what the player sees: each
// time it is needed it is rebuilt from a snapshot of the confirmed world and run forward to the tick on screen, using the
// player's own inputs, which are known, and every other player's last confirmed input, which is a guess. A wrong guess costs
// nothing to repair because the next prediction starts again from the newest confirmed state. No clock, no sockets.
#ifndef PREDICTING_CLIENT_H
#define PREDICTING_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>

#include "Protocol3.h"
#include "RaceWorld.h"

namespace Authority
{
// Anything the prediction can be run on: the headless RaceWorld, or the game's own rendered session.
class PredictedWorld
{
public:
    virtual ~PredictedWorld() {}
    virtual bool RestoreSnapshot(const std::vector<std::uint8_t>& bytes, std::string& error) = 0;
    virtual void StepTick(const std::uint16_t* controls) = 0;   // one 16 ms tick; one control word per player
};

class PredictingClient
{
public:
    PredictingClient();

    bool Init(const std::string& trackPath, const Protocol3::HelloAck& ack, std::string& error);

    // The server's Tick for the next confirmed tick. Returns false if it does not follow the last one or the state hash
    // does not match (the client has diverged and must resynchronise from a snapshot).
    bool OnTick(const Protocol3::Tick& tick);

    // Resynchronise (or join late) from a server snapshot taken at the given tick.
    bool OnSnapshot(const std::vector<std::uint8_t>& snapshot, std::uint32_t tick, std::string& error);

    // What the local player does at a tick (the client sends these; they also drive the prediction).
    void SetLocalInput(std::uint32_t tick, std::uint16_t controls);
    std::uint16_t LocalInput(std::uint32_t tick) const { return mLocalInputs.empty() ? 0 : mLocalInputs[tick < mLocalInputs.size() ? tick : mLocalInputs.size() - 1]; }

    // Rebuilds the predicted world at `targetTick` (>= confirmed tick) and returns it.
    const RaceWorld& Predict(std::uint32_t targetTick);
    // The same on a world of the caller's (the game's rendered session): restore the confirmed snapshot into it and run it
    // forward with the predicted inputs. Returns the number of ticks run, or -1 if the restore failed.
    int PredictOn(PredictedWorld& world, std::uint32_t targetTick, std::string& error);
    const std::vector<std::uint8_t>& ConfirmedSnapshot();   // of the newest confirmed tick (cached)

    const RaceWorld& Confirmed() const { return mConfirmed; }
    std::uint32_t ConfirmedTick() const { return mConfirmed.Tick(); }
    int Player() const { return mPlayer; }
    int Players() const { return mPlayers; }
    std::uint64_t PredictionsRebuilt() const { return mRebuilds; }

private:
    RaceWorld mConfirmed;
    RaceWorld mPredicted;
    int mPlayer;
    int mPlayers;
    std::vector<std::uint16_t> mLocalInputs;        // by tick
    std::vector<std::uint16_t> mLastConfirmed;      // per player: the input they used at the newest confirmed tick
    std::vector<std::uint8_t> mConfirmedSnapshot;   // of the confirmed world, valid for mSnapshotTick
    std::uint32_t mSnapshotTick;
    bool mSnapshotValid;
    std::uint64_t mRebuilds;
};
}

#endif

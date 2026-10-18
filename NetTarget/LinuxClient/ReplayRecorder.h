// Builds a replay file from the messages a client already receives in a race on a HoverNet 3.0 server.
//
// Every Tick message carries the control word of every player for that tick and the state hash after it, so a client that has followed a race
// from its first tick holds everything a replay needs: the same file the server writes (the test compares them byte for byte). Nothing is
// recorded by guesswork: a tick that does not follow the last one (the client joined late, or lost ticks and had to resynchronise) makes the
// recording incomplete, and an incomplete recording is never turned into a file. Header only, no SDL, tested on its own (ReplayRecorderTest.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Authority/Protocol3.h"
#include "../Authority/ReplayFormat.h"

class ReplayRecorder
{
public:
    // A new race: the settings the server sent in its acknowledgement. A replay needs the rules version and the track it was driven on.
    void Begin(const Protocol3::HelloAck& ack)
    {
        mReplay = ReplayFormat::Replay();
        mReplay.track = ack.track;
        mReplay.trackSha256 = ack.trackSha256;
        mReplay.rules = ack.rules;
        mReplay.tickMs = ack.tickMs;
        mReplay.players = ack.players;
        mReplay.laps = ack.laps;
        mReplay.weapons = ack.weapons;
        mNext = 0;
        mComplete = ack.players >= 1 && ack.players <= 8 && ack.tickMs == ReplayFormat::kFrameMs;
        mActive = true;
    }

    // A tick the client has applied. Only the very next tick is accepted; anything else spoils the recording.
    void OnTick(const Protocol3::Tick& tick)
    {
        if (!mActive || !mComplete) return;
        if (tick.tick != mNext || tick.controls.size() != static_cast<std::size_t>(mReplay.players) || mNext >= ReplayFormat::kMaxTicks) { mComplete = false; return; }
        mReplay.inputs.insert(mReplay.inputs.end(), tick.controls.begin(), tick.controls.end());
        ++mNext;
        mReplay.ticks = mNext;
        mReplay.finalHash = tick.hash;
        if (mNext % ReplayFormat::kCheckpointTicks == 0) mReplay.checkpoints.push_back(tick.hash);
    }

    // The client lost ticks and is resynchronising from a snapshot: what follows is not a whole race.
    void Spoil() { mComplete = false; }

    bool Active() const { return mActive; }
    bool Complete() const { return mActive && mComplete && mNext > 0; }
    std::uint32_t Ticks() const { return mNext; }

    // The replay file, or empty when the recording is not whole.
    std::vector<std::uint8_t> Bytes() const
    {
        if (!Complete()) return std::vector<std::uint8_t>();
        return ReplayFormat::Encode(mReplay);
    }

    const ReplayFormat::Replay& Replay() const { return mReplay; }
    void Stop() { mActive = false; }

private:
    ReplayFormat::Replay mReplay;
    std::uint32_t mNext = 0;
    bool mComplete = false;
    bool mActive = false;
};

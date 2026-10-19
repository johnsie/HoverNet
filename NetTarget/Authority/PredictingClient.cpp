#include "PredictingClient.h"

namespace Authority
{
PredictingClient::PredictingClient() : mPlayer(0), mPlayers(0), mSnapshotTick(0), mSnapshotValid(false), mRebuilds(0) {}

bool PredictingClient::Init(const std::string& trackPath, const Protocol3::HelloAck& ack, std::string& error)
{
    mPlayer = ack.player;
    mPlayers = ack.players;
    mLastConfirmed.assign(static_cast<std::size_t>(mPlayers), 0);
    mLocalInputs.clear();
    mSnapshotValid = false;
    const std::int64_t startMs = ReplayFormat::StartTimeMs(ack.rules);   // the countdown is part of the race (rules 7)
    return mConfirmed.Build(trackPath, ack.track, mPlayers, ack.laps, ack.weapons, error, startMs) &&
           mPredicted.Build(trackPath, ack.track, mPlayers, ack.laps, ack.weapons, error, startMs);
}

bool PredictingClient::OnTick(const Protocol3::Tick& tick)
{
    if (tick.tick != mConfirmed.Tick() || static_cast<int>(tick.controls.size()) != mPlayers) return false;
    mConfirmed.Step(tick.controls.data());
    mLastConfirmed = tick.controls;
    mSnapshotValid = false;
    return mConfirmed.Hash() == tick.hash;
}

bool PredictingClient::OnSnapshot(const std::vector<std::uint8_t>& snapshot, std::uint32_t tick, std::string& error)
{
    if (!mConfirmed.Restore(snapshot, error)) return false;
    if (mConfirmed.Tick() != tick) { error = "snapshot tick does not match its time"; return false; }
    mSnapshotValid = false;
    return true;
}

void PredictingClient::SetLocalInput(std::uint32_t tick, std::uint16_t controls)
{
    if (tick >= 1000000) return;
    if (mLocalInputs.size() <= tick) mLocalInputs.resize(static_cast<std::size_t>(tick) + 1, controls);
    mLocalInputs[tick] = controls;
}

const std::vector<std::uint8_t>& PredictingClient::ConfirmedSnapshot()
{
    if (!mSnapshotValid) {
        mConfirmedSnapshot = mConfirmed.Snapshot();
        mSnapshotTick = mConfirmed.Tick();
        mSnapshotValid = true;
    }
    return mConfirmedSnapshot;
}

int PredictingClient::PredictOn(PredictedWorld& world, std::uint32_t targetTick, std::string& error)
{
    const std::vector<std::uint8_t>& snapshot = ConfirmedSnapshot();
    if (!world.RestoreSnapshot(snapshot, error)) return -1;
    ++mRebuilds;
    std::vector<std::uint16_t> controls(static_cast<std::size_t>(mPlayers));
    int steps = 0;
    for (std::uint32_t t = mSnapshotTick; t < targetTick; ++t, ++steps) {
        for (int p = 0; p < mPlayers; ++p) controls[static_cast<std::size_t>(p)] = mLastConfirmed[static_cast<std::size_t>(p)];
        if (!mLocalInputs.empty()) controls[static_cast<std::size_t>(mPlayer)] = mLocalInputs[t < mLocalInputs.size() ? t : mLocalInputs.size() - 1];
        world.StepTick(controls.data());
    }
    return steps;
}

const RaceWorld& PredictingClient::Predict(std::uint32_t targetTick)
{
    if (!mSnapshotValid) {
        mConfirmedSnapshot = mConfirmed.Snapshot();
        mSnapshotTick = mConfirmed.Tick();
        mSnapshotValid = true;
    }
    std::string error;
    mPredicted.Restore(mConfirmedSnapshot, error);
    ++mRebuilds;
    std::vector<std::uint16_t> controls(static_cast<std::size_t>(mPlayers));
    for (std::uint32_t t = mSnapshotTick; t < targetTick; ++t) {
        for (int p = 0; p < mPlayers; ++p) controls[static_cast<std::size_t>(p)] = mLastConfirmed[static_cast<std::size_t>(p)];
        // The local player's input for that tick if known, otherwise their newest known one.
        if (!mLocalInputs.empty()) controls[static_cast<std::size_t>(mPlayer)] = mLocalInputs[t < mLocalInputs.size() ? t : mLocalInputs.size() - 1];
        mPredicted.Step(controls.data());
    }
    return mPredicted;
}
}

// RaceWorld.h -- a complete race (track, craft, simulation) with the few operations a client needs: step, hash, snapshot,
// restore, read a craft. Built on the simulation library only. The server's AuthoritativeRace and the predicting client
// are both made from these.
#ifndef RACE_WORLD_H
#define RACE_WORLD_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MR_GameSession;
class MR_MainCharacter;

namespace Authority
{
class RaceWorld
{
public:
    RaceWorld();
    ~RaceWorld();

    bool Build(const std::string& trackPath, const std::string& trackName, int players, int laps, bool weapons, std::string& error);
    void Step(const std::uint16_t* controls);       // one 16 ms tick; controls has one word per player
    std::uint64_t Hash() const;
    std::vector<std::uint8_t> Snapshot() const;
    bool Restore(const std::vector<std::uint8_t>& bytes, std::string& error);
    std::uint32_t Tick() const { return mTick; }
    int Players() const { return static_cast<int>(mCraft.size()); }
    const MR_MainCharacter* Craft(int player) const { return mCraft[static_cast<std::size_t>(player)]; }

private:
    std::unique_ptr<MR_GameSession> mSession;
    std::vector<MR_MainCharacter*> mCraft;
    std::uint32_t mTick;
};
}

#endif

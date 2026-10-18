// ReplayRecorder builds the replay file from the Tick messages a client receives. The test runs a real authoritative race, turns each tick into
// the message the server sends (the inputs of every player and the state hash after the tick), feeds them to the recorder, and requires the file
// to be byte for byte the one the server itself writes. It also checks that a recording that is not whole (a late start, a gap, a wrong message)
// is never turned into a file.
//
//   HoverNetReplayRecorderTest <track> [players] [ticks]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../MainCharacter/StdAfx.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/WorldCoordinates.h"
#include "../Authority/AuthoritativeRace.h"
#include "../ObjFac1/SimElements.h"
#include "ReplayRecorder.h"
#include "Sha256.h"

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

int main(int argc, char** argv)
{
    const std::string track = argc > 1 ? argv[1] : "ClassicH";
    const int players = argc > 2 ? std::atoi(argv[2]) : 3;
    const int ticks = argc > 3 ? std::atoi(argv[3]) : 700;
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    Authority::RaceConfig config;
    config.trackName = track;
    config.trackPath = "NetTarget/Tracks/" + track + ".trk";
    config.players = players;
    config.laps = 2;
    config.weapons = true;
    Authority::AuthoritativeRace race;
    std::string error;
    if (!race.Start(config, error)) { std::printf("FAIL: cannot start the race: %s\n", error.c_str()); return 1; }

    Protocol3::HelloAck ack;
    ack.players = static_cast<std::uint8_t>(players);
    ack.track = track;
    ack.trackSha256 = Sha256::HashFile(config.trackPath);
    ack.rules = ReplayFormat::kSimulationRules;
    ack.laps = 2;
    ack.weapons = true;

    ReplayRecorder recorder;
    ReplayRecorder lateStart, withGap, wrongSize;
    recorder.Begin(ack); lateStart.Begin(ack); withGap.Begin(ack); wrongSize.Begin(ack);
    Check(!recorder.Complete(), "nothing recorded yet is not a replay");
    for (int t = 0; t < ticks; ++t) {
        Protocol3::Tick message;
        for (int p = 0; p < players; ++p) {
            std::uint16_t c = 1;                                         // motor on
            if ((t / (37 + 16 * p)) % 3 == 0) c |= 4;                    // some steering and shooting, so the inputs differ per player and tick
            if (t % (70 + 20 * p) == 30) c |= 128;
            race.SubmitInput(p, static_cast<std::uint32_t>(t), c);
            message.controls.push_back(c);
        }
        if (!race.AdvanceTick()) break;
        message.tick = static_cast<std::uint32_t>(t);
        message.hash = race.StateHash();
        recorder.OnTick(message);
        if (t >= 5) lateStart.OnTick(message);
        if (t != 100) withGap.OnTick(message);
        if (t == 3) { Protocol3::Tick bad = message; bad.controls.pop_back(); wrongSize.OnTick(bad); }
        else wrongSize.OnTick(message);
    }

    Check(recorder.Complete() && recorder.Ticks() == race.CurrentTick(), "a race followed from its first tick is a whole recording");
    const std::vector<std::uint8_t> mine = recorder.Bytes(), theirs = race.ReplayBytes();
    Check(!mine.empty() && mine == theirs, "the client's replay is byte for byte the server's");
    ReplayFormat::Replay decoded; std::string e;
    Check(ReplayFormat::Decode(mine, decoded, e) && decoded.finalHash == race.StateHash() && decoded.ticks == race.CurrentTick(), "it decodes and ends on the race's final hash");
    Check(!lateStart.Complete() && lateStart.Bytes().empty(), "a client that started late has no replay");
    Check(!withGap.Complete() && withGap.Bytes().empty(), "a gap in the ticks means no replay");
    Check(!wrongSize.Complete() && wrongSize.Bytes().empty(), "a tick without every player's input means no replay");
    ReplayRecorder spoiled; spoiled.Begin(ack); spoiled.Spoil();
    Check(!spoiled.Complete(), "a resynchronisation spoils the recording");
    Protocol3::HelloAck odd = ack; odd.players = 0;
    ReplayRecorder bad; bad.Begin(odd);
    Check(!bad.Complete(), "a race with no players is not recorded");

    if (gFailures == 0) std::printf("replay recorder test passed (%u ticks, %zu bytes)\n", recorder.Ticks(), mine.size());
    return gFailures == 0 ? 0 : 1;
}

// ReplayFormat.h -- HoverNet replay format v1 (docs/specs/replay-format.md): the file structure, its encoder and its strict
// decoder. Used by the replay tool, the server's authoritative race (official replays) and the tests.
#ifndef REPLAY_FORMAT_H
#define REPLAY_FORMAT_H

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayFormat
{
constexpr int kFrameMs = 16;
constexpr std::uint32_t kCheckpointTicks = 250;
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kSimulationRules = 6;  // bump when a deliberate change alters the golden hashes
constexpr std::uint32_t kMaxTicks = 2u * 60 * 60 * 1000 / kFrameMs;  // two hours
constexpr int kMaxPlayers = 8;

struct Replay
{
    std::string track;           // track name as the game knows it
    std::string trackSha256;     // lower-case hex of the .trk file
    std::uint32_t rules = kSimulationRules;
    std::uint32_t tickMs = kFrameMs;
    std::uint32_t ticks = 0;
    int players = 0;
    int laps = 5;                // race settings; stored in the header's flags field (low byte laps, bit 8 weapons)
    bool weapons = true;
    std::vector<std::uint16_t> inputs;                       // [tick * players + player]
    std::vector<std::uint64_t> checkpoints;                  // hash after tick (i+1)*kCheckpointTicks
    std::uint64_t finalHash = 0;
};

std::uint32_t Crc32(const std::uint8_t* data, std::size_t n);
std::vector<std::uint8_t> Encode(const Replay& r);
// Strict: any inconsistency is an error, nothing is read past the end, counts are checked before allocating.
bool Decode(const std::vector<std::uint8_t>& file, Replay& r, std::string& error);
}

#endif

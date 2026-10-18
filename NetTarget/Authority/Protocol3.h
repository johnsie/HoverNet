// Protocol3.h -- HoverNet network protocol 3.0 framing and messages (docs/specs/protocol-3.0-draft.md), transport-free.
//
// Every integer is little-endian and fixed width. A frame is  u16 length | u8 type | payload  where length counts the type
// byte and the payload. Decoders are strict: a message must use exactly the bytes it declares, counts are checked against the
// remaining bytes before anything is allocated, and nothing reads past the end.
#ifndef PROTOCOL3_H
#define PROTOCOL3_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Protocol3
{
constexpr std::uint16_t kMajor = 3;
constexpr std::uint16_t kMinor = 0;
constexpr std::uint16_t kDefaultMaxFrame = 1200;
// Hello.capabilities bits: what this client understands beyond the first 3.0 messages. A server adds the matching optional fields to its
// answers only to a client that set the bit, so a client built before the bit existed never sees a message longer than it expects.
constexpr std::uint32_t kCapRaceId = 1;
constexpr std::uint32_t kCapHostStart = 4;    // the host of a race starts it (StartRace) instead of it starting when the last seat fills; the server sends Roster while it waits
constexpr std::uint32_t kCapLobbyChat = 2;   // the lobby carries chat (Chat / ChatMessage before a race is joined) and sends LobbyUsers   // HelloAck carries the number of the race (so a host can tell friends which private race to join)
constexpr std::uint16_t kMinMaxFrame = 64;
constexpr int kMaxInputsPerMessage = 32;

enum Type : std::uint8_t
{
    kHello = 1,
    kHelloAck = 2,
    kReject = 3,
    kStart = 4,
    kResume = 5,
    kInput = 10,
    kTick = 11,
    kSnapshotRequest = 12,
    kSnapshotChunk = 13,
    kPing = 14,
    kPong = 15,
    kResult = 16,
    kListRaces = 20,
    kRaceList = 21,
    kHostRace = 22,
    kJoinRace = 23,
    kSpectate = 24,        // a Hello that asks to watch the race instead of driving in it
    kSpectateRace = 25,    // lobby: watch a race by id
    kStartRace = 26,       // client to server (the host, only with kCapHostStart): start the race now with the players who are here; no payload
    kChat = 30,            // client to server: say something to the race
    kChatMessage = 31,     // server to client: someone said something
    kBlock = 32,           // client to server: stop showing me this name
    kReport = 33,          // client to server: report a name to the moderators
    kRoster = 35,          // server to client, only with kCapHostStart: who has joined the race that has not started (slot and name), and how many seats
    kListRecords = 44,     // client to server (lobby): the track records of one track; see ListRecords
    kRecordList = 45,      // server to client: the answer, see RecordList
    kListReplays = 40,     // client to server (lobby): list the replays the server keeps (newest first, optionally one track); see ListReplays
    kReplayList = 41,      // server to client: the answer, see ReplayList
    kGetReplay = 42,       // client to server (lobby): send me one replay by id
    kReplayChunk = 43,     // server to client: a piece of the replay file, in order; the last has index + 1 == count
    kLobbyUsers = 34,      // server to client, only to a client that set kCapLobbyChat: who is in the lobby (not yet in a race)
};

enum RejectReason : std::uint8_t
{
    kUnsupportedVersion = 1,
    kServerFull = 2,
    kBadMessage = 3,
    kRateLimited = 4,
    kNotInRace = 5,
    kNoSuchRace = 6,
    kBadSettings = 7,
    kTooManyRaces = 8,
    kBanned = 9,
    kServerDraining = 10,
    kNoSuchReplay = 11,     // the replay is gone (the server keeps only the newest ones); the connection stays open   // the server is shutting down for a restart: races under way finish, no new race starts
};

// ---- framing -----------------------------------------------------------------------------------------------
std::vector<std::uint8_t> EncodeFrame(std::uint8_t type, const std::vector<std::uint8_t>& payload);

// Accumulates bytes (partial and coalesced frames both work) and yields whole frames.
class FrameParser
{
public:
    explicit FrameParser(std::uint16_t maxFrame = kDefaultMaxFrame) : mMaxFrame(maxFrame), mFailed(false) {}
    void SetMaxFrame(std::uint16_t maxFrame) { mMaxFrame = maxFrame; }
    std::uint16_t MaxFrame() const { return mMaxFrame; }
    void Feed(const std::uint8_t* data, std::size_t size);
    // True and fills type and payload when a whole frame is ready. After a framing error (length 0 or above the maximum)
    // Failed() is true and nothing more is returned: the connection must be closed.
    bool Next(std::uint8_t& type, std::vector<std::uint8_t>& payload);
    bool Failed() const { return mFailed; }
    std::size_t Buffered() const { return mBuffer.size(); }

private:
    std::vector<std::uint8_t> mBuffer;
    std::uint16_t mMaxFrame;
    bool mFailed;
};

// ---- messages ----------------------------------------------------------------------------------------------
struct Hello { std::uint16_t major = kMajor, minor = kMinor; std::uint32_t capabilities = 0; std::uint16_t maxFrame = kDefaultMaxFrame; std::string name; };
struct HelloAck { std::uint16_t major = kMajor, minor = kMinor; std::uint32_t capabilities = 0; std::uint16_t maxFrame = kDefaultMaxFrame;
                  std::uint8_t player = 0, players = 0, tickMs = 16; std::uint32_t rules = 0; std::string track; std::string trackSha256; std::uint8_t laps = 0; bool weapons = true; std::uint64_t resumeToken = 0;
                  std::uint32_t raceId = 0; bool includeRaceId = false; };   // raceId: optional trailing field, sent only when the client set kCapRaceId
struct Reject { std::uint8_t reason = 0; std::string message; };
struct Resume { std::uint64_t token = 0; };
struct Start { std::uint32_t countdownMs = 0; };
struct Input { std::uint32_t firstTick = 0; std::vector<std::uint16_t> controls; };            // controls[i] is for tick firstTick + i
struct Tick { std::uint32_t tick = 0; std::uint64_t hash = 0; std::vector<std::uint16_t> controls; };   // the inputs used by every player at `tick`; hash is after it
struct SnapshotRequest { std::uint32_t id = 0; };
struct SnapshotChunk { std::uint32_t id = 0; std::uint16_t index = 0, count = 0; std::uint32_t tick = 0; std::vector<std::uint8_t> data; };
struct Ping { std::uint32_t clientTick = 0, token = 0; };
struct Pong { std::uint32_t clientTick = 0, token = 0, serverTick = 0; };
struct PlayerResult { std::uint8_t player = 0, rank = 0; bool finished = false; std::uint8_t laps = 0; std::int32_t finishTimeMs = 0, bestLapMs = 0; };
struct RaceInfo { std::uint32_t id = 0; std::string track; std::uint8_t players = 0, joined = 0, laps = 0; bool weapons = true, started = false; };
struct RaceList { std::vector<RaceInfo> races; };
struct Roster { std::uint8_t seats = 0; std::vector<std::pair<std::uint8_t, std::string>> members; };   // members: (slot, name) of everyone who has joined, slot 0 being the host
struct LobbyUsers { std::uint16_t total = 0; std::vector<std::string> names; };   // names: the first kMaxLobbyUserNames, in a fixed order; total: how many are in the lobby
constexpr int kMaxLobbyUserNames = 30;
// A non-empty code makes a race private: it is not listed and can be joined or watched only by id and code (6 to 16 characters).
// The code is an optional trailing field, so a message without it is an ordinary public one.
struct HostRace { std::string track; std::uint8_t players = 2, laps = 3; bool weapons = true; std::string code; };
struct JoinRace { std::uint32_t id = 0; std::string code; };
struct SpectateRace { std::uint32_t id = 0; std::string code; };
// Shared replays: the server keeps the newest official replays of the races it ran and offers them to every player in the lobby (the file is
// the same one `HoverNetReplay verify` checks). A replay is named by `id`, the first 8 bytes of the SHA-256 of the file (the number
// `HoverNetReplay id` prints), so the client can check what it received. Replays carry player slots, never names or addresses.
constexpr int kMaxReplayList = 30;
constexpr std::size_t kReplayChunkBytes = 1000;
struct ListReplays { std::string track; std::uint8_t max = 20; };                  // track empty = every track
// One player of a recorded race, in the order they finished (those who did not finish come last, ms 0).
struct ReplayFinisher { std::uint8_t slot = 0; std::int32_t ms = 0; std::uint8_t craft = 255; std::string name; };
struct ReplayInfo { std::uint64_t id = 0; std::string track; std::uint8_t players = 0, laps = 0; std::uint32_t ticks = 0, savedAt = 0, bytes = 0; std::int32_t winnerMs = 0; std::uint8_t winnerSlot = 255; std::string winnerName; std::uint8_t winnerCraft = 255; std::vector<ReplayFinisher> finishers; };   // finishers: everyone in the race, in finishing order, with their display name and craft (empty when the server has no results table for it)   // winnerName: the display name the winner had in the race (empty when not recorded; what the player typed, not proof of who they are). savedAt: unix seconds; winnerMs: the fastest finish, 0 = nobody finished; winnerSlot: that player's slot (255 = nobody), the craft a viewer follows by default
struct ReplayList { std::vector<ReplayInfo> replays; };
struct GetReplay { std::uint64_t id = 0; };
struct ReplayChunk { std::uint64_t id = 0; std::uint16_t index = 0, count = 0; std::vector<std::uint8_t> data; };
// Track records: for each lap count raced on a track, the fastest finishes and the fastest laps among the replays the server keeps, each with the
// replay it came from (so a player can watch the run). Anonymous: a run is a player slot of a replay, never a name.
constexpr int kMaxRecordsPerBoard = 5;    // the best five of each craft
constexpr int kMaxRecordBoards = 8;       // one board per craft (CraftNames.h)
constexpr int kMaxRecordLaps = 12;        // how many different lap counts are offered
// laps 0: the fastest single laps (whatever the length of the race they were driven in); laps n: the fastest finishes of races of n laps. Each answer holds a
// board per craft, best first, so a time in one craft never replaces a time in another.
struct ListRecords { std::string track; std::uint8_t laps = 0; };
struct RecordEntry { std::uint64_t replay = 0; std::uint8_t slot = 0; std::int32_t ms = 0; std::uint32_t savedAt = 0; std::string name; };
struct RecordBoard { std::uint8_t craft = 0; std::vector<RecordEntry> entries; };
struct RecordList { std::string track; std::uint8_t laps = 0; std::vector<std::uint8_t> availableLaps; std::vector<RecordBoard> boards; };
struct Chat { std::string text; };
struct ChatMessage { std::string name; std::string text; bool fromServer = false; };
struct Block { std::string name; };
struct Report { std::string name; std::string reason; };
struct Result { std::uint32_t ticks = 0; std::uint64_t finalHash = 0; std::vector<PlayerResult> players; };

std::vector<std::uint8_t> Encode(const Hello&);
std::vector<std::uint8_t> Encode(const HelloAck&);
std::vector<std::uint8_t> Encode(const Reject&);
std::vector<std::uint8_t> Encode(const Start&);
std::vector<std::uint8_t> Encode(const Resume&);
std::vector<std::uint8_t> Encode(const Input&);
std::vector<std::uint8_t> Encode(const Tick&);
std::vector<std::uint8_t> Encode(const SnapshotRequest&);
std::vector<std::uint8_t> Encode(const SnapshotChunk&);
std::vector<std::uint8_t> Encode(const Ping&);
std::vector<std::uint8_t> Encode(const Pong&);
std::vector<std::uint8_t> Encode(const Result&);
std::vector<std::uint8_t> Encode(const Report&);
std::vector<std::uint8_t> Encode(const Block&);
std::vector<std::uint8_t> Encode(const ChatMessage&);
std::vector<std::uint8_t> Encode(const Chat&);
std::vector<std::uint8_t> Encode(const RaceList&);
std::vector<std::uint8_t> Encode(const ListReplays&);
std::vector<std::uint8_t> Encode(const ListRecords&);
std::vector<std::uint8_t> Encode(const RecordList&);
std::vector<std::uint8_t> Encode(const ReplayList&);
std::vector<std::uint8_t> Encode(const GetReplay&);
std::vector<std::uint8_t> Encode(const ReplayChunk&);
std::vector<std::uint8_t> Encode(const LobbyUsers&);
std::vector<std::uint8_t> Encode(const Roster&);
std::vector<std::uint8_t> Encode(const HostRace&);
std::vector<std::uint8_t> Encode(const JoinRace&);
std::vector<std::uint8_t> Encode(const SpectateRace&);

// Each returns false for anything malformed (wrong size, bad count, bad text) and leaves the output unspecified.
bool Decode(const std::vector<std::uint8_t>& payload, Hello&);
bool Decode(const std::vector<std::uint8_t>& payload, HelloAck&);
bool Decode(const std::vector<std::uint8_t>& payload, Reject&);
bool Decode(const std::vector<std::uint8_t>& payload, Start&);
bool Decode(const std::vector<std::uint8_t>& payload, Resume&);
bool Decode(const std::vector<std::uint8_t>& payload, Input&);
bool Decode(const std::vector<std::uint8_t>& payload, Tick&);
bool Decode(const std::vector<std::uint8_t>& payload, SnapshotRequest&);
bool Decode(const std::vector<std::uint8_t>& payload, SnapshotChunk&);
bool Decode(const std::vector<std::uint8_t>& payload, Ping&);
bool Decode(const std::vector<std::uint8_t>& payload, Pong&);
bool Decode(const std::vector<std::uint8_t>& payload, Result&);
bool Decode(const std::vector<std::uint8_t>& payload, Report&);
bool Decode(const std::vector<std::uint8_t>& payload, Block&);
bool Decode(const std::vector<std::uint8_t>& payload, ChatMessage&);
bool Decode(const std::vector<std::uint8_t>& payload, Chat&);
bool Decode(const std::vector<std::uint8_t>& payload, RaceList&);
bool Decode(const std::vector<std::uint8_t>& payload, ListReplays&);
bool Decode(const std::vector<std::uint8_t>& payload, ListRecords&);
bool Decode(const std::vector<std::uint8_t>& payload, RecordList&);
bool Decode(const std::vector<std::uint8_t>& payload, ReplayList&);
bool Decode(const std::vector<std::uint8_t>& payload, GetReplay&);
bool Decode(const std::vector<std::uint8_t>& payload, ReplayChunk&);
bool Decode(const std::vector<std::uint8_t>& payload, LobbyUsers&);
bool Decode(const std::vector<std::uint8_t>& payload, Roster&);
bool Decode(const std::vector<std::uint8_t>& payload, HostRace&);
bool Decode(const std::vector<std::uint8_t>& payload, JoinRace&);
bool Decode(const std::vector<std::uint8_t>& payload, SpectateRace&);

// Text fields (names, track names, messages): UTF-8, one line, no control bytes, at most `maxBytes`. Rejects overlong
// forms, surrogates, code points above U+10FFFF and truncated sequences.
bool IsValidText(const std::string& text, std::size_t maxBytes);
}

#endif

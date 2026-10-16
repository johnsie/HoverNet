// Protocol3.h -- HoverNet network protocol 3.0 framing and messages (docs/specs/protocol-3.0-draft.md), transport-free.
//
// Every integer is little-endian and fixed width. A frame is  u16 length | u8 type | payload  where length counts the type
// byte and the payload. Decoders are strict: a message must use exactly the bytes it declares, counts are checked against the
// remaining bytes before anything is allocated, and nothing reads past the end.
#ifndef PROTOCOL3_H
#define PROTOCOL3_H

#include <cstdint>
#include <string>
#include <vector>

namespace Protocol3
{
constexpr std::uint16_t kMajor = 3;
constexpr std::uint16_t kMinor = 0;
constexpr std::uint16_t kDefaultMaxFrame = 1200;
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
};

enum RejectReason : std::uint8_t
{
    kUnsupportedVersion = 1,
    kServerFull = 2,
    kBadMessage = 3,
    kRateLimited = 4,
    kNotInRace = 5,
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
                  std::uint8_t player = 0, players = 0, tickMs = 16; std::uint32_t rules = 0; std::string track; std::string trackSha256; std::uint8_t laps = 0; bool weapons = true; std::uint64_t resumeToken = 0; };
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

// Text fields (names, track names, messages): UTF-8, one line, no control bytes, at most `maxBytes`. Rejects overlong
// forms, surrogates, code points above U+10FFFF and truncated sequences.
bool IsValidText(const std::string& text, std::size_t maxBytes);
}

#endif

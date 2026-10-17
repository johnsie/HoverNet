// SecureChannel.h -- HoverNet's secure session (docs/specs/secure-session.md): a byte stream that is encrypted, authenticated and
// forward secret, where the client knows the server's public key in advance and so cannot be tricked into talking to an impostor.
// It is a Noise_NK-style handshake over the primitives in Crypto.h; it sits underneath the protocol 3.0 frames and knows nothing
// about them. Transport-free: callers feed received bytes in and send the bytes that come out.
//
//   client -> server   "HNS1" | client ephemeral public key (32)
//   server -> client   server ephemeral public key (32) | one record "HNS1-READY"
//   then both ways     records:  u16 length | ciphertext | 16-byte tag      (length counts ciphertext and tag)
//
// Keys come from HKDF over X25519(client ephemeral, server static) and X25519(client ephemeral, server ephemeral), salted with
// a hash of the handshake. Each direction has its own key and a counter used as the AEAD nonce, so a record can neither be
// replayed, reordered, removed nor altered without the receiver noticing; the length is authenticated as associated data.
#ifndef SECURE_CHANNEL_H
#define SECURE_CHANNEL_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Crypto.h"

namespace Secure
{
constexpr std::size_t kMaxPlain = 1400;                  // plaintext bytes in one record
constexpr std::size_t kHandshakeClient = 4 + 32;
extern const char kMagic[4];                              // "HNS1"

// One direction's record layer.
class Records
{
public:
    Records() : mCounter(0) { std::memset(mKey, 0, sizeof(mKey)); }
    void SetKey(const std::uint8_t key[32]) { std::memcpy(mKey, key, 32); mCounter = 0; }
    Crypto::Bytes Seal(const std::uint8_t* data, std::size_t size);     // splits into records
    // Appends received bytes and decrypts every whole record into `plain`. False if a record is oversize or fails authentication.
    bool Open(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain);
    std::uint64_t Counter() const { return mCounter; }

private:
    std::uint8_t mKey[32];
    std::uint64_t mCounter;
    Crypto::Bytes mInbox;
};

class Client
{
public:
    explicit Client(const std::uint8_t serverPublic[32]);
    Crypto::Bytes Hello();                                  // the bytes to send first
    // Feed bytes from the server; decrypted application bytes are appended to `plain`. False on any failure (wrong server key,
    // damaged or replayed data): the connection must be closed.
    bool Feed(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain);
    Crypto::Bytes Seal(const std::uint8_t* data, std::size_t size) { return mSend.Seal(data, size); }
    bool Ready() const { return mReady; }

private:
    std::uint8_t mServerPublic[32], mSecret[32], mPublic[32];
    Crypto::Bytes mInbox;
    Records mSend, mRecv;
    bool mHaveKeys, mReady, mFailed;
};

class Server
{
public:
    explicit Server(const std::uint8_t secret[32]);
    // Feed bytes from the client; application bytes go to `plain`, handshake output (the reply) to `reply`.
    bool Feed(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain, Crypto::Bytes& reply);
    Crypto::Bytes Seal(const std::uint8_t* data, std::size_t size) { return mSend.Seal(data, size); }
    bool Ready() const { return mReady; }
    // True if the first bytes of a connection are the start of a secure handshake rather than a plain protocol frame.
    static bool LooksLikeHandshake(const std::uint8_t* data, std::size_t size);

private:
    std::uint8_t mSecret[32], mPublic[32];
    Crypto::Bytes mInbox;
    Records mSend, mRecv;
    bool mReady, mFailed;
};

// The server's identity: 32 secret bytes in a file as 64 hex characters, created (with the owner-only permissions) if missing.
bool LoadOrCreateIdentity(const std::string& path, std::uint8_t secret[32], std::string& error);
}

#endif

#include "SecureChannel.h"

#include <cstdio>
#include <fstream>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../LinuxClient/Sha256.h"

namespace Secure
{
const char kMagic[4] = {'H', 'N', 'S', '1'};

namespace
{
void Nonce(std::uint64_t counter, std::uint8_t nonce[12])
{
    std::memset(nonce, 0, 12);
    for (int i = 0; i < 8; ++i) nonce[4 + i] = static_cast<std::uint8_t>(counter >> (8 * i));
}

bool AllZero(const std::uint8_t* p, std::size_t n)
{
    std::uint8_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) acc |= p[i];
    return acc == 0;
}

// Both sides derive the same two keys from the two shared secrets and the handshake transcript.
void DeriveKeys(const std::uint8_t es[32], const std::uint8_t ee[32], const std::uint8_t clientPublic[32], const std::uint8_t serverEphemeral[32],
                const std::uint8_t serverPublic[32], std::uint8_t clientToServer[32], std::uint8_t serverToClient[32])
{
    std::string transcript(kMagic, 4);
    transcript.append(reinterpret_cast<const char*>(clientPublic), 32);
    transcript.append(reinterpret_cast<const char*>(serverEphemeral), 32);
    transcript.append(reinterpret_cast<const char*>(serverPublic), 32);
    const std::string saltHex = Sha256::HashBytes(transcript.data(), transcript.size());
    Crypto::Bytes salt;
    Crypto::FromHex(saltHex, salt);
    Crypto::Bytes ikm(es, es + 32);
    ikm.insert(ikm.end(), ee, ee + 32);
    const Crypto::Bytes prk = Crypto::HkdfExtract(salt, ikm);
    const std::string c2s = "hns1 client to server", s2c = "hns1 server to client";
    const Crypto::Bytes a = Crypto::HkdfExpand(prk, Crypto::Bytes(c2s.begin(), c2s.end()), 32);
    const Crypto::Bytes b = Crypto::HkdfExpand(prk, Crypto::Bytes(s2c.begin(), s2c.end()), 32);
    std::memcpy(clientToServer, a.data(), 32);
    std::memcpy(serverToClient, b.data(), 32);
}
}

// ---- records -------------------------------------------------------------------------------------------------
Crypto::Bytes Records::Seal(const std::uint8_t* data, std::size_t size)
{
    Crypto::Bytes out;
    std::size_t offset = 0;
    do {
        const std::size_t n = size - offset < kMaxPlain ? size - offset : kMaxPlain;
        const std::uint16_t length = static_cast<std::uint16_t>(n + 16);
        const std::uint8_t aad[2] = {static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(length >> 8)};
        std::uint8_t nonce[12];
        Nonce(mCounter++, nonce);
        out.insert(out.end(), aad, aad + 2);
        const std::size_t at = out.size();
        out.resize(at + n + 16);
        Crypto::AeadSeal(mKey, nonce, aad, 2, data + offset, n, out.data() + at, out.data() + at + n);
        offset += n;
    } while (offset < size);
    return out;
}

bool Records::Open(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain)
{
    mInbox.insert(mInbox.end(), data, data + size);
    std::size_t pos = 0;
    while (mInbox.size() - pos >= 2) {
        const std::size_t length = mInbox[pos] | (static_cast<std::size_t>(mInbox[pos + 1]) << 8);
        if (length < 16 || length > kMaxPlain + 16) return false;
        if (mInbox.size() - pos < 2 + length) break;
        const std::size_t n = length - 16;
        std::uint8_t nonce[12];
        Nonce(mCounter, nonce);
        const std::size_t at = plain.size();
        plain.resize(at + n);
        if (!Crypto::AeadOpen(mKey, nonce, &mInbox[pos], 2, &mInbox[pos + 2], n, &mInbox[pos + 2 + n], plain.data() + at)) { plain.resize(at); return false; }
        ++mCounter;
        pos += 2 + length;
    }
    mInbox.erase(mInbox.begin(), mInbox.begin() + static_cast<std::ptrdiff_t>(pos));
    return true;
}

// ---- client --------------------------------------------------------------------------------------------------
Client::Client(const std::uint8_t serverPublic[32]) : mHaveKeys(false), mReady(false), mFailed(false)
{
    std::memcpy(mServerPublic, serverPublic, 32);
    if (!Crypto::RandomBytes(mSecret, 32)) mFailed = true;
    Crypto::X25519Public(mPublic, mSecret);
}

Crypto::Bytes Client::Hello()
{
    Crypto::Bytes out(kMagic, kMagic + 4);
    out.insert(out.end(), mPublic, mPublic + 32);
    return out;
}

bool Client::Feed(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain)
{
    if (mFailed) return false;
    static const char ready[] = "HNS1-READY";
    const std::size_t readyLength = sizeof(ready) - 1;
    Crypto::Bytes got;
    if (!mHaveKeys) {
        mInbox.insert(mInbox.end(), data, data + size);
        if (mInbox.size() < 32) return true;
        std::uint8_t es[32], ee[32], serverEphemeral[32], c2s[32], s2c[32];
        std::memcpy(serverEphemeral, mInbox.data(), 32);
        Crypto::X25519(es, mSecret, mServerPublic);
        Crypto::X25519(ee, mSecret, serverEphemeral);
        if (AllZero(es, 32) || AllZero(ee, 32)) { mFailed = true; return false; }
        DeriveKeys(es, ee, mPublic, serverEphemeral, mServerPublic, c2s, s2c);
        mSend.SetKey(c2s);
        mRecv.SetKey(s2c);
        mHaveKeys = true;
        const Crypto::Bytes rest(mInbox.begin() + 32, mInbox.end());
        mInbox.clear();
        if (!mRecv.Open(rest.data(), rest.size(), got)) { mFailed = true; return false; }
    }
    else if (!mRecv.Open(data, size, got)) {
        mFailed = true;
        return false;
    }
    if (!mReady) {
        // The first record is the server's proof that it holds the private key matching the public key we pinned.
        mInbox.insert(mInbox.end(), got.begin(), got.end());
        if (mInbox.size() < readyLength) return true;
        if (std::memcmp(mInbox.data(), ready, readyLength) != 0) { mFailed = true; return false; }
        mReady = true;
        got.assign(mInbox.begin() + static_cast<std::ptrdiff_t>(readyLength), mInbox.end());
        mInbox.clear();
    }
    plain.insert(plain.end(), got.begin(), got.end());
    return true;
}

// ---- server --------------------------------------------------------------------------------------------------
Server::Server(const std::uint8_t secret[32]) : mReady(false), mFailed(false)
{
    std::memcpy(mSecret, secret, 32);
    Crypto::X25519Public(mPublic, mSecret);
}

bool Server::LooksLikeHandshake(const std::uint8_t* data, std::size_t size)
{
    if (size == 0) return false;
    const std::size_t n = size < 4 ? size : 4;
    return std::memcmp(data, kMagic, n) == 0;
}

bool Server::Feed(const std::uint8_t* data, std::size_t size, Crypto::Bytes& plain, Crypto::Bytes& reply)
{
    if (mFailed) return false;
    if (!mReady) {
        mInbox.insert(mInbox.end(), data, data + size);
        if (mInbox.size() < kHandshakeClient) return true;
        if (std::memcmp(mInbox.data(), kMagic, 4) != 0) { mFailed = true; return false; }
        std::uint8_t clientPublic[32], serverEphemeralSecret[32], serverEphemeral[32], es[32], ee[32], c2s[32], s2c[32];
        std::memcpy(clientPublic, mInbox.data() + 4, 32);
        if (!Crypto::RandomBytes(serverEphemeralSecret, 32)) { mFailed = true; return false; }
        Crypto::X25519Public(serverEphemeral, serverEphemeralSecret);
        Crypto::X25519(es, mSecret, clientPublic);
        Crypto::X25519(ee, serverEphemeralSecret, clientPublic);
        if (AllZero(es, 32) || AllZero(ee, 32)) { mFailed = true; return false; }
        DeriveKeys(es, ee, clientPublic, serverEphemeral, mPublic, c2s, s2c);
        mSend.SetKey(s2c);
        mRecv.SetKey(c2s);
        reply.insert(reply.end(), serverEphemeral, serverEphemeral + 32);
        static const char ready[] = "HNS1-READY";
        const Crypto::Bytes proof = mSend.Seal(reinterpret_cast<const std::uint8_t*>(ready), sizeof(ready) - 1);
        reply.insert(reply.end(), proof.begin(), proof.end());
        mReady = true;
        const Crypto::Bytes rest(mInbox.begin() + kHandshakeClient, mInbox.end());
        mInbox.clear();
        if (!rest.empty() && !mRecv.Open(rest.data(), rest.size(), plain)) { mFailed = true; return false; }
        return true;
    }
    if (!mRecv.Open(data, size, plain)) { mFailed = true; return false; }
    return true;
}

// ---- identity ------------------------------------------------------------------------------------------------
bool LoadOrCreateIdentity(const std::string& path, std::uint8_t secret[32], std::string& error)
{
    {
        std::ifstream in(path.c_str());
        std::string hex;
        if (in && std::getline(in, hex)) {
            while (!hex.empty() && (hex.back() == '\r' || hex.back() == ' ')) hex.pop_back();
            Crypto::Bytes bytes;
            if (Crypto::FromHex(hex, bytes) && bytes.size() == 32) { std::memcpy(secret, bytes.data(), 32); return true; }
            error = "the identity file is not 64 hex characters";
            return false;
        }
    }
    if (!Crypto::RandomBytes(secret, 32)) { error = "no secure random source on this machine"; return false; }
    std::ofstream out(path.c_str());
    if (!out) { error = "cannot create the identity file " + path; return false; }
    out << Crypto::ToHex(secret, 32) << "\n";
    out.close();
#ifndef _WIN32
    chmod(path.c_str(), 0600);   // the key is the server's identity: only its owner may read it
#endif
    return true;
}
}

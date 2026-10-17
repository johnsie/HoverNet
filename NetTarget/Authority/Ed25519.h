// Ed25519.h -- digital signatures (RFC 8032, pure Ed25519) with SHA-512, for signed catalogues and release files.
//
// Written out from the public specification in the compact style of TweetNaCl (public domain) and checked against the RFC 8032 test
// vectors and against an independent reference implementation (HoverNetEd25519Test, scripts/test-signing.sh). It favours being small and
// readable over speed, and its field arithmetic is the constant-time TweetNaCl form, but it has not been audited. Used only to sign
// and verify files; the secure session does not use it.
#ifndef AUTHORITY_ED25519_H
#define AUTHORITY_ED25519_H

#include <cstddef>
#include <cstdint>

namespace Ed25519
{
void Sha512(const std::uint8_t* data, std::size_t size, std::uint8_t out[64]);
// The public key for a 32-byte seed (the "secret key" of RFC 8032).
void PublicKey(const std::uint8_t seed[32], std::uint8_t publicKey[32]);
void Sign(const std::uint8_t seed[32], const std::uint8_t* message, std::size_t size, std::uint8_t signature[64]);
bool Verify(const std::uint8_t publicKey[32], const std::uint8_t* message, std::size_t size, const std::uint8_t signature[64]);
}

#endif

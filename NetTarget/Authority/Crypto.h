// Crypto.h -- the primitives of HoverNet's secure session, written out in full from the public specifications and checked against
// their test vectors (HoverNetCryptoTest): ChaCha20, Poly1305 and the AEAD built from them (RFC 8439), X25519 (RFC 7748) and HKDF
// with SHA-256 (RFC 5869). No other cryptographic code is used. The project's own SHA-256 and HMAC-SHA256 are the hash.
//
// Nothing here is a general-purpose library: it exists so a server and a client can authenticate each other and encrypt a stream
// without a third-party dependency. See docs/specs/secure-session.md.
#ifndef CRYPTO_H
#define CRYPTO_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Crypto
{
typedef std::vector<std::uint8_t> Bytes;

void ChaCha20Block(const std::uint8_t key[32], std::uint32_t counter, const std::uint8_t nonce[12], std::uint8_t out[64]);
void ChaCha20Xor(const std::uint8_t key[32], std::uint32_t counter, const std::uint8_t nonce[12], const std::uint8_t* in, std::size_t size, std::uint8_t* out);
void Poly1305(const std::uint8_t key[32], const std::uint8_t* message, std::size_t size, std::uint8_t tag[16]);

// RFC 8439 AEAD: ciphertext is plaintext-sized, the 16-byte tag is separate.
void AeadSeal(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint8_t* aad, std::size_t aadSize,
              const std::uint8_t* plain, std::size_t size, std::uint8_t* cipher, std::uint8_t tag[16]);
bool AeadOpen(const std::uint8_t key[32], const std::uint8_t nonce[12], const std::uint8_t* aad, std::size_t aadSize,
              const std::uint8_t* cipher, std::size_t size, const std::uint8_t tag[16], std::uint8_t* plain);

void X25519(std::uint8_t out[32], const std::uint8_t scalar[32], const std::uint8_t point[32]);
void X25519Public(std::uint8_t out[32], const std::uint8_t secret[32]);   // scalar times the base point

Bytes HkdfExtract(const Bytes& salt, const Bytes& ikm);
Bytes HkdfExpand(const Bytes& prk, const Bytes& info, std::size_t length);

// Secure random bytes from the operating system (std::random_device). Returns false if the system has no usable source.
bool RandomBytes(std::uint8_t* out, std::size_t size);

std::string ToHex(const std::uint8_t* data, std::size_t size);
bool FromHex(const std::string& hex, Bytes& out);
bool ConstantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t size);
}

#endif

# Secure session v1

Status: **implemented and tested** (`NetTarget/Authority/Crypto.*`, `SecureChannel.*`; ctest `HoverNetCrypto`, `HoverNetSecureTcp`).
It gives protocol 3.0 an encrypted, authenticated, forward-secret transport for native clients. (Browsers use `wss://`; see below.)

## What it protects

- **Confidentiality and integrity** of everything after the handshake: chat, resume tokens, inputs, snapshots.
- **Server authentication.** The client knows the server's public key beforehand (pinned) and can only complete the session with a
  server that holds the matching private key, so a machine in the middle cannot pose as the server.
- **Forward secrecy.** The keys depend on fresh ephemeral keys on both sides; recording the traffic and later stealing the server's key does
  not reveal it.
- **Replay and reordering protection.** Each direction has its own key and a record counter used as the nonce.

It does **not** hide who is talking to whom, the sizes and timing of records, or that a connection is a HoverNet one. It does not
authenticate the *client* (that is the optional sign-in's job, Stage 6a, not built) and it does not stop a server operator reading their own
traffic.

## Handshake (a Noise_NK-style pattern)

    client -> server   "HNS1" | client ephemeral public key (32 bytes)
    server -> client   server ephemeral public key (32 bytes) | one record containing "HNS1-READY"

    es = X25519(client ephemeral secret, server static public)      ee = X25519(client ephemeral secret, server ephemeral public)
    salt = SHA-256("HNS1" | client ephemeral | server ephemeral | server static)
    prk  = HKDF-Extract(salt, es | ee)
    key client->server = HKDF-Expand(prk, "hns1 client to server", 32)      key server->client = HKDF-Expand(prk, "hns1 server to client", 32)

The client proves nothing to the server at this stage; the server proves possession of the static key by being able to compute `es` and
produce a valid first record. A client that pinned another key fails at that record and closes. All-zero shared secrets (low-order points)
are refused.

## Records

    u16 length (little endian, = ciphertext + 16)  |  ciphertext  |  16-byte Poly1305 tag

ChaCha20-Poly1305 (RFC 8439); the nonce is four zero bytes then the record counter (64 bits, little endian), the two length bytes are the
associated data, a record carries at most 1,400 bytes of the protocol stream, and any failure (bad tag, wrong counter, oversize length) ends
the connection. The protocol 3.0 frames travel inside the records unchanged.

## Telling secure from plain

A plain protocol frame can never start with the bytes `HN` (as a length that is above the maximum frame), so a server that has an identity
accepts both on one port and decides on the first two bytes. `--require-encryption` refuses plain connections.

## Identity and keys

The server's identity is 32 random bytes kept as 64 hex characters in a file (`--identity-file`, created with owner-only permissions if
missing). The server prints its public key as `server key <64 hex>`; players pin it with `--server-key <hex>` (the game, the bot) and a
server list will carry it. Rotating the key means players must be given the new one. There is no certificate authority and no revocation
beyond that.

## Implementation notes and verification

All primitives are written out in `Crypto.cpp` from the RFCs (no third-party code, no new dependency) and are checked against the published
vectors: ChaCha20 block (RFC 8439 2.3.2), Poly1305 (2.5.2), the AEAD (2.8.2), X25519 (RFC 7748 6.1 and 5.2) and HKDF (RFC 5869 case 1). The
session is tested for correct operation with the stream delivered a byte at a time and in odd chunks; for refusing a wrong server key, a
damaged reply, a reordered record, a replayed record, and every possible single-byte change of a record; and against 3,000 inputs of random
bytes. `scripts/test-secure-tcp.sh` runs it over TCP: bots and the game client with the right key race normally, a plain client and a
client with the wrong key are refused, and a capture of the traffic holds no readable track name or player name.

Limits and honest caveats: the code has been tested but not independently audited; the X25519 field arithmetic is the straightforward
TweetNaCl-style form and is written to be constant-time in its control flow, but timing behaviour has not been measured; random bytes come
from `std::random_device`, which is the operating system's generator on the supported platforms. An independent security review is a Stage 8
requirement and should cover this file first.

## Browsers

The browser client cannot do raw TCP; it reaches the gateway over `wss://`, where the web server's TLS provides the same properties. The
gateway forwards frames unchanged, so no secure session is run inside it.

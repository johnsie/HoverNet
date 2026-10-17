// Tests of HoverNet's cryptography and secure session (NetTarget/Authority/Crypto.*, SecureChannel.*): the primitives against the
// published test vectors of RFC 8439, RFC 7748 and RFC 5869, and the session for the properties it must have: both ends read what the
// other writes, in any chunking; a wrong server key, a damaged, replayed, reordered, truncated or injected record, and an impostor
// are all refused.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../Authority/Crypto.h"
#include "../Authority/SecureChannel.h"

using namespace Crypto;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

Bytes Hex(const char* text) { Bytes b; FromHex(text, b); return b; }
Bytes Str(const std::string& s) { return Bytes(s.begin(), s.end()); }
}

int main()
{
    // ---- ChaCha20 block, RFC 8439 2.3.2 ------------------------------------------------------------------------
    {
        const Bytes key = Hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        const Bytes nonce = Hex("000000090000004a00000000");
        std::uint8_t out[64];
        ChaCha20Block(key.data(), 1, nonce.data(), out);
        Check(ToHex(out, 64) == "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e",
              "ChaCha20 block (RFC 8439 2.3.2)");
    }
    // ---- Poly1305, RFC 8439 2.5.2 ------------------------------------------------------------------------------
    {
        const Bytes key = Hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b");
        const Bytes msg = Str("Cryptographic Forum Research Group");
        std::uint8_t tag[16];
        Poly1305(key.data(), msg.data(), msg.size(), tag);
        Check(ToHex(tag, 16) == "a8061dc1305136c6c22b8baf0c0127a9", "Poly1305 (RFC 8439 2.5.2)");
    }
    // ---- AEAD, RFC 8439 2.8.2 ----------------------------------------------------------------------------------
    {
        const Bytes key = Hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
        const Bytes nonce = Hex("070000004041424344454647");
        const Bytes aad = Hex("50515253c0c1c2c3c4c5c6c7");
        const Bytes plain = Str("Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.");
        Bytes cipher(plain.size());
        std::uint8_t tag[16];
        AeadSeal(key.data(), nonce.data(), aad.data(), aad.size(), plain.data(), plain.size(), cipher.data(), tag);
        Check(ToHex(tag, 16) == "1ae10b594f09e26a7e902ecbd0600691", "AEAD tag (RFC 8439 2.8.2)");
        Check(ToHex(cipher.data(), 16) == "d31a8d34648e60db7b86afbc53ef7ec2", "AEAD ciphertext start (RFC 8439 2.8.2)");
        Bytes back(plain.size());
        Check(AeadOpen(key.data(), nonce.data(), aad.data(), aad.size(), cipher.data(), cipher.size(), tag, back.data()) && back == plain, "AEAD opens what it sealed");
        cipher[5] ^= 1;
        Check(!AeadOpen(key.data(), nonce.data(), aad.data(), aad.size(), cipher.data(), cipher.size(), tag, back.data()), "AEAD refuses a changed ciphertext");
        cipher[5] ^= 1;
        std::uint8_t badAad[12];
        std::memcpy(badAad, aad.data(), 12);
        badAad[0] ^= 1;
        Check(!AeadOpen(key.data(), nonce.data(), badAad, 12, cipher.data(), cipher.size(), tag, back.data()), "AEAD refuses changed associated data");
    }
    // ---- X25519, RFC 7748 6.1 and 5.2 --------------------------------------------------------------------------
    {
        const Bytes alicePrivate = Hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
        const Bytes bobPrivate = Hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
        std::uint8_t alicePublic[32], bobPublic[32], sharedA[32], sharedB[32];
        X25519Public(alicePublic, alicePrivate.data());
        X25519Public(bobPublic, bobPrivate.data());
        Check(ToHex(alicePublic, 32) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", "X25519 Alice public key (RFC 7748 6.1)");
        Check(ToHex(bobPublic, 32) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", "X25519 Bob public key (RFC 7748 6.1)");
        X25519(sharedA, alicePrivate.data(), bobPublic);
        X25519(sharedB, bobPrivate.data(), alicePublic);
        Check(ToHex(sharedA, 32) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742" && std::memcmp(sharedA, sharedB, 32) == 0, "X25519 shared secret (RFC 7748 6.1)");
        // 5.2, first iteration: k = u = 9.
        std::uint8_t k[32] = {9}, u[32] = {9}, next[32];
        X25519(next, k, u);
        Check(ToHex(next, 32) == "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", "X25519 iteration (RFC 7748 5.2)");
    }
    // ---- HKDF, RFC 5869 test case 1 ----------------------------------------------------------------------------
    {
        const Bytes ikm = Hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
        const Bytes salt = Hex("000102030405060708090a0b0c");
        const Bytes info = Hex("f0f1f2f3f4f5f6f7f8f9");
        const Bytes prk = HkdfExtract(salt, ikm);
        Check(ToHex(prk.data(), prk.size()) == "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", "HKDF-Extract (RFC 5869 case 1)");
        const Bytes okm = HkdfExpand(prk, info, 42);
        Check(ToHex(okm.data(), okm.size()) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", "HKDF-Expand (RFC 5869 case 1)");
    }

    // ---- the session ----------------------------------------------------------------------------------------------
    {
        std::uint8_t serverSecret[32], serverPublic[32];
        RandomBytes(serverSecret, 32);
        X25519Public(serverPublic, serverSecret);

        auto run = [&](std::size_t chunk, std::size_t messageSize) {
            Secure::Client client(serverPublic);
            Secure::Server server(serverSecret);
            Bytes toServer = client.Hello(), toClient, clientGot, serverGot, reply;
            // Deliver the client's bytes to the server in chunks, and the replies back in chunks.
            for (std::size_t i = 0; i < toServer.size(); i += chunk) {
                const std::size_t n = std::min(chunk, toServer.size() - i);
                if (!server.Feed(toServer.data() + i, n, serverGot, reply)) return false;
            }
            for (std::size_t i = 0; i < reply.size(); i += chunk) {
                const std::size_t n = std::min(chunk, reply.size() - i);
                if (!client.Feed(reply.data() + i, n, clientGot)) return false;
            }
            if (!client.Ready() || !server.Ready()) return false;
            Bytes message(messageSize);
            for (std::size_t i = 0; i < message.size(); ++i) message[i] = static_cast<std::uint8_t>(i * 7 + 3);
            const Bytes wire = client.Seal(message.data(), message.size());
            const Bytes wire2 = server.Seal(message.data(), message.size());
            Bytes unused;
            for (std::size_t i = 0; i < wire.size(); i += chunk) if (!server.Feed(wire.data() + i, std::min(chunk, wire.size() - i), serverGot, unused)) return false;
            for (std::size_t i = 0; i < wire2.size(); i += chunk) if (!client.Feed(wire2.data() + i, std::min(chunk, wire2.size() - i), clientGot)) return false;
            return serverGot == message && clientGot == message;
        };
        Check(run(1, 100), "a session works with the stream delivered one byte at a time");
        Check(run(7, 5000), "a session works with odd chunks and a message of several records");
        Check(run(100000, 3), "a session works with everything delivered at once");
        Check(run(64, 0), "an empty message is fine");

        // Wrong server key: the impostor cannot produce the proof.
        {
            std::uint8_t otherSecret[32], otherPublic[32];
            RandomBytes(otherSecret, 32);
            X25519Public(otherPublic, otherSecret);
            Secure::Client client(serverPublic);      // pins the real server's key
            Secure::Server impostor(otherSecret);     // but talks to someone with another key
            Bytes hello = client.Hello(), plain, reply, got;
            impostor.Feed(hello.data(), hello.size(), plain, reply);
            Check(!client.Feed(reply.data(), reply.size(), got) || !client.Ready(), "a client pinned to one key refuses a server with another");
        }
        // Tampering with the server's reply or its records.
        {
            Secure::Client client(serverPublic);
            Secure::Server server(serverSecret);
            Bytes hello = client.Hello(), plain, reply, got;
            server.Feed(hello.data(), hello.size(), plain, reply);
            Bytes bad = reply;
            bad[bad.size() - 1] ^= 1;
            Check(!client.Feed(bad.data(), bad.size(), got), "a damaged handshake reply is refused");
        }
        {
            Secure::Client client(serverPublic);
            Secure::Server server(serverSecret);
            Bytes hello = client.Hello(), plain, reply, got;
            server.Feed(hello.data(), hello.size(), plain, reply);
            client.Feed(reply.data(), reply.size(), got);
            Bytes a = client.Seal(reinterpret_cast<const std::uint8_t*>("first"), 5), b = client.Seal(reinterpret_cast<const std::uint8_t*>("second"), 6);
            Bytes out, unused;
            Check(server.Feed(b.data(), b.size(), out, unused) == false, "a record delivered out of order is refused");
        }
        {
            Secure::Client client(serverPublic);
            Secure::Server server(serverSecret);
            Bytes hello = client.Hello(), plain, reply, got;
            server.Feed(hello.data(), hello.size(), plain, reply);
            client.Feed(reply.data(), reply.size(), got);
            Bytes a = client.Seal(reinterpret_cast<const std::uint8_t*>("first"), 5);
            Bytes out, unused;
            Check(server.Feed(a.data(), a.size(), out, unused), "the first record is accepted");
            Check(server.Feed(a.data(), a.size(), out, unused) == false, "the same record replayed is refused");
        }
        {
            Secure::Client client(serverPublic);
            Secure::Server server(serverSecret);
            Bytes hello = client.Hello(), plain, reply, got;
            server.Feed(hello.data(), hello.size(), plain, reply);
            client.Feed(reply.data(), reply.size(), got);
            Bytes a = client.Seal(reinterpret_cast<const std::uint8_t*>("payload"), 7);
            for (std::size_t i = 0; i < a.size(); ++i) {
                Secure::Server fresh(serverSecret);
                Secure::Client c2(serverPublic);
                Bytes h2 = c2.Hello(), p2, r2, g2;
                fresh.Feed(h2.data(), h2.size(), p2, r2);
                c2.Feed(r2.data(), r2.size(), g2);
                Bytes w = c2.Seal(reinterpret_cast<const std::uint8_t*>("payload"), 7);
                w[i] ^= 0x01;
                Bytes out, unused;
                // Every changed byte must be caught (a changed length byte may leave the record incomplete, which is also not accepted data).
                if (fresh.Feed(w.data(), w.size(), out, unused) && !out.empty()) { std::fprintf(stderr, "FAIL: a changed byte %zu was accepted as data\n", i); ++gFailures; break; }
            }
        }
        // Handshake detection and truncation.
        const std::uint8_t plainFrame[] = {0x20, 0x00, 0x01};
        Check(!Secure::Server::LooksLikeHandshake(plainFrame, sizeof(plainFrame)), "a plain protocol frame is not mistaken for a handshake");
        Check(Secure::Server::LooksLikeHandshake(reinterpret_cast<const std::uint8_t*>("HNS1"), 4) && Secure::Server::LooksLikeHandshake(reinterpret_cast<const std::uint8_t*>("HN"), 2), "a handshake is recognised, even in pieces");
        // Fuzz: random bytes into a server never crash and never yield data.
        std::uint64_t s = 5;
        auto next = [&s]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; };
        for (int round = 0; round < 3000; ++round) {
            Secure::Server server(serverSecret);
            Bytes junk(1 + next() % 300);
            for (auto& b : junk) b = static_cast<std::uint8_t>(next());
            if (round % 2 == 0) { junk.insert(junk.begin(), Secure::kMagic, Secure::kMagic + 4); }
            Bytes out, reply;
            server.Feed(junk.data(), junk.size(), out, reply);
            if (!out.empty()) { std::fprintf(stderr, "FAIL: junk produced application data\n"); ++gFailures; break; }
        }
        // Identity file.
        const std::string path = "hovernet-test-identity.key";
        std::remove(path.c_str());
        std::uint8_t a[32], b[32];
        std::string error;
        Check(Secure::LoadOrCreateIdentity(path, a, error) && Secure::LoadOrCreateIdentity(path, b, error) && std::memcmp(a, b, 32) == 0, "an identity is created once and loaded after");
        std::remove(path.c_str());
    }

    std::printf("crypto tests %s\n", gFailures == 0 ? "passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}

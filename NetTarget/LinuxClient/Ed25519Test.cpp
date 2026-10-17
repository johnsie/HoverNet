// Known-answer tests for the SHA-512 and Ed25519 implementation (RFC 8032 section 7.1 vectors 1 and 2), plus tampering.
//
//   HoverNetEd25519Test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../Authority/Ed25519.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

std::vector<std::uint8_t> FromHex(const std::string& t)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < t.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(t.substr(i, 2), nullptr, 16)));
    return out;
}

void Vector(const char* name, const char* seedHex, const char* pubHex, const char* msgHex, const char* sigHex)
{
    const std::vector<std::uint8_t> seed = FromHex(seedHex), pub = FromHex(pubHex), msg = FromHex(msgHex), sig = FromHex(sigHex);
    std::uint8_t gotPub[32], gotSig[64];
    Ed25519::PublicKey(seed.data(), gotPub);
    Check(std::memcmp(gotPub, pub.data(), 32) == 0, (std::string(name) + ": public key").c_str());
    Ed25519::Sign(seed.data(), msg.data(), msg.size(), gotSig);
    Check(std::memcmp(gotSig, sig.data(), 64) == 0, (std::string(name) + ": signature").c_str());
    Check(Ed25519::Verify(pub.data(), msg.data(), msg.size(), sig.data()), (std::string(name) + ": verifies").c_str());
    std::vector<std::uint8_t> bad = sig;
    bad[10] ^= 1;
    Check(!Ed25519::Verify(pub.data(), msg.data(), msg.size(), bad.data()), (std::string(name) + ": a changed R is refused").c_str());
    bad = sig; bad[40] ^= 1;
    Check(!Ed25519::Verify(pub.data(), msg.data(), msg.size(), bad.data()), (std::string(name) + ": a changed S is refused").c_str());
    std::vector<std::uint8_t> otherMsg = msg;
    otherMsg.push_back(0);
    Check(!Ed25519::Verify(pub.data(), otherMsg.data(), otherMsg.size(), sig.data()), (std::string(name) + ": a changed message is refused").c_str());
}
}

int main()
{
    std::uint8_t d[64];
    Ed25519::Sha512(reinterpret_cast<const std::uint8_t*>("abc"), 3, d);
    Check(std::memcmp(d, FromHex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f").data(), 64) == 0, "SHA-512 of abc");
    Ed25519::Sha512(nullptr, 0, d);
    Check(std::memcmp(d, FromHex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e").data(), 64) == 0, "SHA-512 of nothing");
    Vector("RFC 8032 test 1", "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
           "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    Vector("RFC 8032 test 2", "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
           "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    std::printf("ed25519 tests %s\n", gFailures == 0 ? "passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}

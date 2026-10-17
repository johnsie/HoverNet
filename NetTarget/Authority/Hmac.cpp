#include "Hmac.h"

#include <algorithm>
#include <vector>

#include "../LinuxClient/Sha256.h"

namespace Hmac
{
namespace
{
std::vector<unsigned char> FromHex(const std::string& hex)
{
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
        out.push_back(static_cast<unsigned char>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
    }
    return out;
}
}

std::string Sha256Hex(const std::string& key, const std::string& message)
{
    std::vector<unsigned char> k(64, 0);
    if (key.size() > 64) {
        const std::vector<unsigned char> d = FromHex(Sha256::HashBytes(key.data(), key.size()));
        std::copy(d.begin(), d.end(), k.begin());
    }
    else {
        std::copy(key.begin(), key.end(), k.begin());
    }
    std::vector<unsigned char> inner(64), outer(64);
    for (int i = 0; i < 64; ++i) { inner[i] = k[i] ^ 0x36; outer[i] = k[i] ^ 0x5c; }
    inner.insert(inner.end(), message.begin(), message.end());
    const std::vector<unsigned char> innerDigest = FromHex(Sha256::HashBytes(inner.data(), inner.size()));
    outer.insert(outer.end(), innerDigest.begin(), innerDigest.end());
    return Sha256::HashBytes(outer.data(), outer.size());
}

std::vector<std::uint8_t> Sha256Raw(const std::string& key, const std::string& message)
{
    const std::vector<unsigned char> raw = FromHex(Sha256Hex(key, message));
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
}

bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
}

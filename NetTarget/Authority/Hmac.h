// Hmac.h -- HMAC-SHA256 (RFC 2104) for authenticating moderators; built on the project's SHA-256.
#ifndef HMAC_H
#define HMAC_H

#include <cstdint>
#include <string>
#include <vector>

namespace Hmac
{
// Lower-case hex of HMAC-SHA256(key, message).
std::string Sha256Hex(const std::string& key, const std::string& message);
// The same as raw bytes.
std::vector<std::uint8_t> Sha256Raw(const std::string& key, const std::string& message);
// Compares two hex strings in time that does not depend on where they differ.
bool ConstantTimeEquals(const std::string& a, const std::string& b);
}

#endif

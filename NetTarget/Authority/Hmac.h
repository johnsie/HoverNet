// Hmac.h -- HMAC-SHA256 (RFC 2104) for authenticating moderators; built on the project's SHA-256.
#ifndef HMAC_H
#define HMAC_H

#include <string>

namespace Hmac
{
// Lower-case hex of HMAC-SHA256(key, message).
std::string Sha256Hex(const std::string& key, const std::string& message);
// Compares two hex strings in time that does not depend on where they differ.
bool ConstantTimeEquals(const std::string& a, const std::string& b);
}

#endif

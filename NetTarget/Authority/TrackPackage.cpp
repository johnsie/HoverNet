#include "TrackPackage.h"

#include "Protocol3.h"
#include "ReplayFormat.h"
#include "../LinuxClient/Sha256.h"

namespace TrackPackage
{
namespace
{
void U16(std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
void U32(std::vector<std::uint8_t>& b, std::uint32_t v) { U16(b, v); U16(b, v >> 16); }
std::uint32_t R16(const std::vector<std::uint8_t>& b, std::size_t p) { return b[p] | (static_cast<std::uint32_t>(b[p + 1]) << 8); }
std::uint32_t R32(const std::vector<std::uint8_t>& b, std::size_t p) { return R16(b, p) | (R16(b, p + 2) << 16); }
}

bool IsValidManifestText(const std::string& text)
{
    return Protocol3::IsValidText(text, 200);
}

std::vector<std::uint8_t> Encode(const Package& package)
{
    std::string manifest;
    for (const auto& kv : package.manifest) manifest += kv.first + "=" + kv.second + "\n";   // std::map is sorted
    std::vector<std::uint8_t> out = {'H', 'N', 'T', 'P'};
    U16(out, kVersion);
    U16(out, 0);
    U32(out, static_cast<std::uint32_t>(manifest.size()));
    out.insert(out.end(), manifest.begin(), manifest.end());
    U32(out, static_cast<std::uint32_t>(package.payload.size()));
    out.insert(out.end(), package.payload.begin(), package.payload.end());
    U32(out, ReplayFormat::Crc32(out.data(), out.size()));
    return out;
}

bool Decode(const std::vector<std::uint8_t>& file, Package& package, std::string& error)
{
    package = Package();
    if (file.size() < 4 + 2 + 2 + 4 + 4 + 4) { error = "file too short"; return false; }
    const std::size_t body = file.size() - 4;
    if (R32(file, body) != ReplayFormat::Crc32(file.data(), body)) { error = "CRC mismatch (damaged file)"; return false; }
    if (file[0] != 'H' || file[1] != 'N' || file[2] != 'T' || file[3] != 'P') { error = "not a HoverNet track package"; return false; }
    if (R16(file, 4) != kVersion) { error = "unsupported package version " + std::to_string(R16(file, 4)); return false; }
    if (R16(file, 6) != 0) { error = "unknown flags"; return false; }
    std::size_t pos = 8;
    const std::uint32_t manifestLength = R32(file, pos);
    pos += 4;
    if (manifestLength > kMaxManifestBytes || manifestLength > body - pos) { error = "bad manifest length"; return false; }
    const std::string manifest(file.begin() + static_cast<std::ptrdiff_t>(pos), file.begin() + static_cast<std::ptrdiff_t>(pos + manifestLength));
    pos += manifestLength;
    if (body - pos < 4) { error = "truncated"; return false; }
    const std::uint32_t payloadLength = R32(file, pos);
    pos += 4;
    if (payloadLength > kMaxPayloadBytes || payloadLength != body - pos) { error = "bad payload length"; return false; }
    package.payload.assign(file.begin() + static_cast<std::ptrdiff_t>(pos), file.begin() + static_cast<std::ptrdiff_t>(body));

    // Manifest: lines of key=value, keys sorted and unique, text valid.
    std::size_t start = 0;
    std::string previous;
    while (start < manifest.size()) {
        const std::size_t end = manifest.find('\n', start);
        if (end == std::string::npos) { error = "manifest does not end with a newline"; return false; }
        const std::string line = manifest.substr(start, end - start);
        start = end + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) { error = "bad manifest line"; return false; }
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        for (char c : key) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) { error = "bad manifest key"; return false; }
        if (!IsValidManifestText(value)) { error = "bad manifest value for " + key; return false; }
        if (!previous.empty() && key <= previous) { error = "manifest keys are not sorted and unique"; return false; }
        previous = key;
        package.manifest[key] = value;
    }
    static const char* required[] = {"format", "name", "author", "licence", "payload-sha256", "payload-bytes", "mode", "rooms", "starts"};
    for (const char* k : required) if (!package.manifest.count(k)) { error = std::string("manifest lacks ") + k; return false; }
    if (package.manifest["format"] != "1") { error = "unsupported track format in the manifest"; return false; }
    if (package.manifest["payload-bytes"] != std::to_string(package.payload.size())) { error = "payload size does not match the manifest"; return false; }
    if (package.manifest["payload-sha256"] != Sha256::HashBytes(package.payload.data(), package.payload.size())) { error = "payload hash does not match the manifest"; return false; }
    return true;
}
}

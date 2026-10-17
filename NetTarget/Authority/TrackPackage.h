// TrackPackage.h -- HoverNet track package v1 (docs/specs/track-package.md): a track with its metadata in one verifiable file.
//
// Layout (all integers little-endian): magic "HNTP", u16 version (1), u16 flags (0), u32 manifest length, the manifest (UTF-8,
// lines of "key=value", keys sorted, no duplicates), u32 payload length, the payload (the classic .trk bytes, unmodified), u32
// CRC-32 of everything before it. The manifest records what the payload is (name, author, licence, format, hashes, what the
// importer found); the payload is never altered, so a package can always give back the exact original file.
#ifndef TRACK_PACKAGE_H
#define TRACK_PACKAGE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace TrackPackage
{
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxManifestBytes = 16 * 1024;
constexpr std::uint32_t kMaxPayloadBytes = 64 * 1024 * 1024;

struct Package
{
    std::map<std::string, std::string> manifest;
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> Encode(const Package& package);
// Strict: checks the magic, version, flags, lengths against the remaining bytes, the manifest syntax and the CRC; the manifest
// must carry the required keys and a payload hash and size that match the payload. Fills `error` on failure.
bool Decode(const std::vector<std::uint8_t>& file, Package& package, std::string& error);

// Required manifest keys: format, name, author, licence, payload-sha256, payload-bytes, mode, rooms, starts.
bool IsValidManifestText(const std::string& text);   // one line of UTF-8 without control bytes, at most 200 bytes
}

#endif

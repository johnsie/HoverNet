// Gunzip.h -- minimal gzip (RFC 1952) / DEFLATE (RFC 1951) decoder.
//
// Community tracks are downloaded as .trk.gz (about a quarter of the size). This
// avoids a zlib dependency on every platform. Input is untrusted, so decoding is
// fully bounds-checked and the output is capped.

#ifndef HOVERNET_GUNZIP_H
#define HOVERNET_GUNZIP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Decodes one gzip member from pInput. Fails (returning false and setting
// pError) on malformed data, a CRC-32 or size mismatch, or output larger than
// pMaxOutput bytes.
bool GunzipBuffer(const std::vector<std::uint8_t>& pInput, std::vector<std::uint8_t>& pOutput,
                  std::size_t pMaxOutput, std::string& pError);

// Reads pInputPath (a .gz file) and writes the decoded bytes to pOutputPath.
bool GunzipFile(const std::string& pInputPath, const std::string& pOutputPath, std::size_t pMaxOutput,
                std::string& pError);

#endif

// Sha256.h -- small self-contained SHA-256, used to verify downloaded tracks.

#ifndef HOVERNET_SHA256_H
#define HOVERNET_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

class Sha256
{
public:
    Sha256();
    void Update(const void* pData, std::size_t pLength);
    // Lower-case hex digest. The object must not be updated afterwards.
    std::string Finish();

    static std::string HashBytes(const void* pData, std::size_t pLength);
    // Hex digest of a file's contents, or an empty string if it cannot be read.
    static std::string HashFile(const std::string& pPath);

private:
    void Block(const std::uint8_t* pBlock);

    std::uint32_t mState[8];
    std::uint8_t mBuffer[64];
    std::size_t mBuffered;
    std::uint64_t mTotalBytes;
};

#endif

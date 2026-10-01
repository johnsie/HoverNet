// Sha256.cpp -- FIPS 180-4 SHA-256.

#include "Sha256.h"

#include <cstring>
#include <fstream>

namespace {

const std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t Rotr(std::uint32_t pValue, int pBits)
{
    return (pValue >> pBits) | (pValue << (32 - pBits));
}

} // namespace

Sha256::Sha256()
    : mBuffered(0), mTotalBytes(0)
{
    static const std::uint32_t kInitial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(mState, kInitial, sizeof(mState));
}

void Sha256::Block(const std::uint8_t* pBlock)
{
    std::uint32_t lWords[64];
    for (int lIndex = 0; lIndex < 16; ++lIndex) {
        lWords[lIndex] = (static_cast<std::uint32_t>(pBlock[lIndex * 4]) << 24) |
                         (static_cast<std::uint32_t>(pBlock[lIndex * 4 + 1]) << 16) |
                         (static_cast<std::uint32_t>(pBlock[lIndex * 4 + 2]) << 8) |
                         static_cast<std::uint32_t>(pBlock[lIndex * 4 + 3]);
    }
    for (int lIndex = 16; lIndex < 64; ++lIndex) {
        const std::uint32_t lS0 = Rotr(lWords[lIndex - 15], 7) ^ Rotr(lWords[lIndex - 15], 18) ^ (lWords[lIndex - 15] >> 3);
        const std::uint32_t lS1 = Rotr(lWords[lIndex - 2], 17) ^ Rotr(lWords[lIndex - 2], 19) ^ (lWords[lIndex - 2] >> 10);
        lWords[lIndex] = lWords[lIndex - 16] + lS0 + lWords[lIndex - 7] + lS1;
    }

    std::uint32_t a = mState[0], b = mState[1], c = mState[2], d = mState[3];
    std::uint32_t e = mState[4], f = mState[5], g = mState[6], h = mState[7];
    for (int lIndex = 0; lIndex < 64; ++lIndex) {
        const std::uint32_t lS1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const std::uint32_t lChoice = (e & f) ^ (~e & g);
        const std::uint32_t lTemp1 = h + lS1 + lChoice + kRound[lIndex] + lWords[lIndex];
        const std::uint32_t lS0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const std::uint32_t lMajority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t lTemp2 = lS0 + lMajority;
        h = g; g = f; f = e; e = d + lTemp1;
        d = c; c = b; b = a; a = lTemp1 + lTemp2;
    }
    mState[0] += a; mState[1] += b; mState[2] += c; mState[3] += d;
    mState[4] += e; mState[5] += f; mState[6] += g; mState[7] += h;
}

void Sha256::Update(const void* pData, std::size_t pLength)
{
    const std::uint8_t* lBytes = static_cast<const std::uint8_t*>(pData);
    mTotalBytes += pLength;
    while (pLength > 0) {
        const std::size_t lRoom = 64 - mBuffered;
        const std::size_t lTake = pLength < lRoom ? pLength : lRoom;
        std::memcpy(mBuffer + mBuffered, lBytes, lTake);
        mBuffered += lTake;
        lBytes += lTake;
        pLength -= lTake;
        if (mBuffered == 64) {
            Block(mBuffer);
            mBuffered = 0;
        }
    }
}

std::string Sha256::Finish()
{
    const std::uint64_t lBits = mTotalBytes * 8;
    const std::uint8_t lPad = 0x80;
    Update(&lPad, 1);
    const std::uint8_t lZero = 0;
    while (mBuffered != 56) {
        Update(&lZero, 1);
    }
    std::uint8_t lLength[8];
    for (int lIndex = 0; lIndex < 8; ++lIndex) {
        lLength[lIndex] = static_cast<std::uint8_t>(lBits >> (56 - 8 * lIndex));
    }
    Update(lLength, 8);

    static const char kHex[] = "0123456789abcdef";
    std::string lDigest;
    for (int lWord = 0; lWord < 8; ++lWord) {
        for (int lByte = 3; lByte >= 0; --lByte) {
            const std::uint8_t lValue = static_cast<std::uint8_t>(mState[lWord] >> (8 * lByte));
            lDigest += kHex[lValue >> 4];
            lDigest += kHex[lValue & 15];
        }
    }
    return lDigest;
}

std::string Sha256::HashBytes(const void* pData, std::size_t pLength)
{
    Sha256 lHash;
    lHash.Update(pData, pLength);
    return lHash.Finish();
}

std::string Sha256::HashFile(const std::string& pPath)
{
    std::ifstream lFile(pPath.c_str(), std::ios::binary);
    if (!lFile.good()) {
        return std::string();
    }
    Sha256 lHash;
    char lChunk[65536];
    while (lFile.read(lChunk, sizeof(lChunk)) || lFile.gcount() > 0) {
        lHash.Update(lChunk, static_cast<std::size_t>(lFile.gcount()));
    }
    return lHash.Finish();
}

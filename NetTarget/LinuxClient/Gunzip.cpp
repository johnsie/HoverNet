// Gunzip.cpp

#include "Gunzip.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

struct BitReader
{
    const std::uint8_t* mData;
    std::size_t mSize;
    std::size_t mPosition = 0;   // next byte
    std::uint32_t mBitBuffer = 0;
    int mBitCount = 0;
    bool mFailed = false;

    int Bit()
    {
        if (mBitCount == 0) {
            if (mPosition >= mSize) {
                mFailed = true;
                return 0;
            }
            mBitBuffer = mData[mPosition++];
            mBitCount = 8;
        }
        const int lBit = static_cast<int>(mBitBuffer & 1);
        mBitBuffer >>= 1;
        --mBitCount;
        return lBit;
    }

    std::uint32_t Bits(int pCount)
    {
        std::uint32_t lValue = 0;
        for (int lIndex = 0; lIndex < pCount; ++lIndex) {
            lValue |= static_cast<std::uint32_t>(Bit()) << lIndex;
        }
        return lValue;
    }

    void AlignToByte()
    {
        mBitCount = 0;
        mBitBuffer = 0;
    }
};

// Canonical Huffman decoding table (counts per length + symbols in code order).
struct Huffman
{
    std::uint16_t mCount[16];
    std::uint16_t mSymbol[288];
};

bool BuildHuffman(Huffman& pTable, const std::uint8_t* pLengths, int pCount)
{
    std::memset(pTable.mCount, 0, sizeof(pTable.mCount));
    for (int lIndex = 0; lIndex < pCount; ++lIndex) {
        ++pTable.mCount[pLengths[lIndex]];
    }
    pTable.mCount[0] = 0;

    int lLeft = 1;
    for (int lLength = 1; lLength < 16; ++lLength) {
        lLeft <<= 1;
        lLeft -= pTable.mCount[lLength];
        if (lLeft < 0) {
            return false; // over-subscribed
        }
    }

    std::uint16_t lOffset[16];
    lOffset[1] = 0;
    for (int lLength = 1; lLength < 15; ++lLength) {
        lOffset[lLength + 1] = static_cast<std::uint16_t>(lOffset[lLength] + pTable.mCount[lLength]);
    }
    for (int lIndex = 0; lIndex < pCount; ++lIndex) {
        if (pLengths[lIndex] != 0) {
            pTable.mSymbol[lOffset[pLengths[lIndex]]++] = static_cast<std::uint16_t>(lIndex);
        }
    }
    return true;
}

int DecodeSymbol(BitReader& pReader, const Huffman& pTable)
{
    int lCode = 0, lFirst = 0, lIndex = 0;
    for (int lLength = 1; lLength < 16; ++lLength) {
        lCode |= pReader.Bit();
        if (pReader.mFailed) {
            return -1;
        }
        const int lCount = pTable.mCount[lLength];
        if (lCode - lCount < lFirst) {
            return pTable.mSymbol[lIndex + (lCode - lFirst)];
        }
        lIndex += lCount;
        lFirst += lCount;
        lFirst <<= 1;
        lCode <<= 1;
    }
    return -1;
}

const std::uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                         6145, 8193, 12289, 16385, 24577};
const std::uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool InflateBlock(BitReader& pReader, const Huffman& pLiteral, const Huffman& pDistance,
                  std::vector<std::uint8_t>& pOut, std::size_t pMax, std::string& pError)
{
    while (true) {
        const int lSymbol = DecodeSymbol(pReader, pLiteral);
        if (lSymbol < 0) {
            pError = "corrupt compressed data";
            return false;
        }
        if (lSymbol < 256) {
            if (pOut.size() >= pMax) {
                pError = "decompressed data too large";
                return false;
            }
            pOut.push_back(static_cast<std::uint8_t>(lSymbol));
        }
        else if (lSymbol == 256) {
            return true;
        }
        else {
            const int lLengthIndex = lSymbol - 257;
            if (lLengthIndex >= 29) {
                pError = "corrupt compressed data";
                return false;
            }
            const std::size_t lLength = kLengthBase[lLengthIndex] + pReader.Bits(kLengthExtra[lLengthIndex]);
            const int lDistanceSymbol = DecodeSymbol(pReader, pDistance);
            if (lDistanceSymbol < 0 || lDistanceSymbol >= 30) {
                pError = "corrupt compressed data";
                return false;
            }
            const std::size_t lDistance = kDistanceBase[lDistanceSymbol] + pReader.Bits(kDistanceExtra[lDistanceSymbol]);
            if (pReader.mFailed || lDistance > pOut.size()) {
                pError = "corrupt compressed data";
                return false;
            }
            if (pOut.size() + lLength > pMax) {
                pError = "decompressed data too large";
                return false;
            }
            const std::size_t lStart = pOut.size() - lDistance;
            for (std::size_t lIndex = 0; lIndex < lLength; ++lIndex) {
                pOut.push_back(pOut[lStart + lIndex]);
            }
        }
    }
}

bool Inflate(const std::uint8_t* pData, std::size_t pSize, std::size_t& pConsumed,
             std::vector<std::uint8_t>& pOut, std::size_t pMax, std::string& pError)
{
    BitReader lReader;
    lReader.mData = pData;
    lReader.mSize = pSize;

    int lFinal = 0;
    do {
        lFinal = lReader.Bit();
        const std::uint32_t lType = lReader.Bits(2);
        if (lReader.mFailed) {
            pError = "truncated compressed data";
            return false;
        }

        if (lType == 0) {
            lReader.AlignToByte();
            if (lReader.mPosition + 4 > pSize) {
                pError = "truncated compressed data";
                return false;
            }
            const std::size_t lLength = pData[lReader.mPosition] | (pData[lReader.mPosition + 1] << 8);
            const std::size_t lInverse = pData[lReader.mPosition + 2] | (pData[lReader.mPosition + 3] << 8);
            lReader.mPosition += 4;
            if ((lLength ^ 0xFFFF) != lInverse || lReader.mPosition + lLength > pSize) {
                pError = "corrupt compressed data";
                return false;
            }
            if (pOut.size() + lLength > pMax) {
                pError = "decompressed data too large";
                return false;
            }
            pOut.insert(pOut.end(), pData + lReader.mPosition, pData + lReader.mPosition + lLength);
            lReader.mPosition += lLength;
        }
        else if (lType == 1 || lType == 2) {
            Huffman lLiteral, lDistance;
            if (lType == 1) {
                std::uint8_t lLengths[288];
                for (int lIndex = 0; lIndex < 144; ++lIndex) lLengths[lIndex] = 8;
                for (int lIndex = 144; lIndex < 256; ++lIndex) lLengths[lIndex] = 9;
                for (int lIndex = 256; lIndex < 280; ++lIndex) lLengths[lIndex] = 7;
                for (int lIndex = 280; lIndex < 288; ++lIndex) lLengths[lIndex] = 8;
                BuildHuffman(lLiteral, lLengths, 288);
                std::uint8_t lDistanceLengths[30];
                std::memset(lDistanceLengths, 5, sizeof(lDistanceLengths));
                BuildHuffman(lDistance, lDistanceLengths, 30);
            }
            else {
                const int lLiteralCount = static_cast<int>(lReader.Bits(5)) + 257;
                const int lDistanceCount = static_cast<int>(lReader.Bits(5)) + 1;
                const int lCodeLengthCount = static_cast<int>(lReader.Bits(4)) + 4;
                if (lLiteralCount > 286 || lDistanceCount > 30) {
                    pError = "corrupt compressed data";
                    return false;
                }
                static const std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                std::uint8_t lCodeLengths[19];
                std::memset(lCodeLengths, 0, sizeof(lCodeLengths));
                for (int lIndex = 0; lIndex < lCodeLengthCount; ++lIndex) {
                    lCodeLengths[kOrder[lIndex]] = static_cast<std::uint8_t>(lReader.Bits(3));
                }
                Huffman lCodeTable;
                if (!BuildHuffman(lCodeTable, lCodeLengths, 19)) {
                    pError = "corrupt compressed data";
                    return false;
                }
                std::uint8_t lLengths[320];
                std::memset(lLengths, 0, sizeof(lLengths));
                int lIndex = 0;
                while (lIndex < lLiteralCount + lDistanceCount) {
                    const int lSymbol = DecodeSymbol(lReader, lCodeTable);
                    if (lSymbol < 0) {
                        pError = "corrupt compressed data";
                        return false;
                    }
                    if (lSymbol < 16) {
                        lLengths[lIndex++] = static_cast<std::uint8_t>(lSymbol);
                        continue;
                    }
                    std::uint8_t lValue = 0;
                    int lRepeat = 0;
                    if (lSymbol == 16) {
                        if (lIndex == 0) {
                            pError = "corrupt compressed data";
                            return false;
                        }
                        lValue = lLengths[lIndex - 1];
                        lRepeat = 3 + static_cast<int>(lReader.Bits(2));
                    }
                    else if (lSymbol == 17) {
                        lRepeat = 3 + static_cast<int>(lReader.Bits(3));
                    }
                    else {
                        lRepeat = 11 + static_cast<int>(lReader.Bits(7));
                    }
                    if (lIndex + lRepeat > lLiteralCount + lDistanceCount) {
                        pError = "corrupt compressed data";
                        return false;
                    }
                    while (lRepeat-- > 0) {
                        lLengths[lIndex++] = lValue;
                    }
                }
                if (lLengths[256] == 0 || !BuildHuffman(lLiteral, lLengths, lLiteralCount) ||
                    !BuildHuffman(lDistance, lLengths + lLiteralCount, lDistanceCount)) {
                    pError = "corrupt compressed data";
                    return false;
                }
            }
            if (!InflateBlock(lReader, lLiteral, lDistance, pOut, pMax, pError)) {
                return false;
            }
        }
        else {
            pError = "corrupt compressed data";
            return false;
        }
    } while (!lFinal);

    pConsumed = lReader.mPosition;
    return true;
}

std::uint32_t Crc32(const std::uint8_t* pData, std::size_t pSize)
{
    static std::uint32_t sTable[256];
    static bool sBuilt = false;
    if (!sBuilt) {
        for (std::uint32_t lIndex = 0; lIndex < 256; ++lIndex) {
            std::uint32_t lValue = lIndex;
            for (int lBit = 0; lBit < 8; ++lBit) {
                lValue = (lValue & 1) ? (0xEDB88320u ^ (lValue >> 1)) : (lValue >> 1);
            }
            sTable[lIndex] = lValue;
        }
        sBuilt = true;
    }
    std::uint32_t lCrc = 0xFFFFFFFFu;
    for (std::size_t lIndex = 0; lIndex < pSize; ++lIndex) {
        lCrc = sTable[(lCrc ^ pData[lIndex]) & 0xFF] ^ (lCrc >> 8);
    }
    return lCrc ^ 0xFFFFFFFFu;
}

} // namespace

bool GunzipBuffer(const std::vector<std::uint8_t>& pInput, std::vector<std::uint8_t>& pOutput,
                  std::size_t pMaxOutput, std::string& pError)
{
    pOutput.clear();
    const std::size_t lSize = pInput.size();
    if (lSize < 18 || pInput[0] != 0x1f || pInput[1] != 0x8b || pInput[2] != 8) {
        pError = "not a gzip file";
        return false;
    }
    const std::uint8_t lFlags = pInput[3];
    std::size_t lPosition = 10;
    if (lFlags & 4) { // FEXTRA
        if (lPosition + 2 > lSize) { pError = "truncated gzip header"; return false; }
        lPosition += 2 + (pInput[lPosition] | (pInput[lPosition + 1] << 8));
    }
    for (int lSkipString = 0; lSkipString < 2; ++lSkipString) { // FNAME, FCOMMENT
        if (lFlags & (lSkipString == 0 ? 8 : 16)) {
            while (lPosition < lSize && pInput[lPosition] != 0) ++lPosition;
            ++lPosition;
        }
    }
    if (lFlags & 2) lPosition += 2; // FHCRC
    if (lPosition + 8 > lSize) {
        pError = "truncated gzip header";
        return false;
    }

    std::size_t lConsumed = 0;
    if (!Inflate(pInput.data() + lPosition, lSize - lPosition - 8, lConsumed, pOutput, pMaxOutput, pError)) {
        pOutput.clear();
        return false;
    }
    const std::size_t lTrailer = lPosition + lConsumed;
    if (lTrailer + 8 > lSize) {
        pError = "truncated gzip data";
        pOutput.clear();
        return false;
    }
    const std::uint32_t lExpectedCrc = pInput[lTrailer] | (pInput[lTrailer + 1] << 8) |
                                       (pInput[lTrailer + 2] << 16) |
                                       (static_cast<std::uint32_t>(pInput[lTrailer + 3]) << 24);
    const std::uint32_t lExpectedSize = pInput[lTrailer + 4] | (pInput[lTrailer + 5] << 8) |
                                        (pInput[lTrailer + 6] << 16) |
                                        (static_cast<std::uint32_t>(pInput[lTrailer + 7]) << 24);
    if (lExpectedSize != static_cast<std::uint32_t>(pOutput.size()) ||
        lExpectedCrc != Crc32(pOutput.data(), pOutput.size())) {
        pError = "downloaded file is corrupt";
        pOutput.clear();
        return false;
    }
    return true;
}

bool GunzipFile(const std::string& pInputPath, const std::string& pOutputPath, std::size_t pMaxOutput,
                std::string& pError)
{
    std::ifstream lIn(pInputPath.c_str(), std::ios::binary);
    if (!lIn.good()) {
        pError = "cannot read downloaded file";
        return false;
    }
    std::vector<std::uint8_t> lInput((std::istreambuf_iterator<char>(lIn)), std::istreambuf_iterator<char>());
    std::vector<std::uint8_t> lOutput;
    if (!GunzipBuffer(lInput, lOutput, pMaxOutput, pError)) {
        return false;
    }
    std::ofstream lOut(pOutputPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!lOut.good()) {
        pError = "cannot write track file";
        return false;
    }
    lOut.write(reinterpret_cast<const char*>(lOutput.data()), static_cast<std::streamsize>(lOutput.size()));
    lOut.close();
    if (!lOut.good()) {
        pError = "cannot write track file";
        return false;
    }
    return true;
}

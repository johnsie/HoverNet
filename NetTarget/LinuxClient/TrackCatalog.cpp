// TrackCatalog.cpp

#include "TrackCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>

namespace {

// Matches the wire limit for a track name (see RaceServer's kMaxTrackNameBytes).
const std::size_t kMaxTrackNameBytes = 63;

std::string Lower(const std::string& pText)
{
    std::string lResult = pText;
    for (char& lChar : lResult) {
        lChar = static_cast<char>(std::tolower(static_cast<unsigned char>(lChar)));
    }
    return lResult;
}

bool FileExists(const std::string& pPath)
{
    std::ifstream lFile(pPath.c_str(), std::ios::binary);
    return lFile.good();
}

std::string FindTrackFile(const std::vector<std::string>& pDirectories, const std::string& pName)
{
    for (const std::string& lDirectory : pDirectories) {
        const std::string lPath = lDirectory + pName + ".trk";
        if (FileExists(lPath)) {
            return lPath;
        }
    }
    return std::string();
}

std::vector<std::string> SplitTabs(const std::string& pLine)
{
    std::vector<std::string> lFields;
    std::size_t lStart = 0;
    while (true) {
        const std::size_t lEnd = pLine.find('\t', lStart);
        if (lEnd == std::string::npos) {
            lFields.push_back(pLine.substr(lStart));
            break;
        }
        lFields.push_back(pLine.substr(lStart, lEnd - lStart));
        lStart = lEnd + 1;
    }
    return lFields;
}

int ToInt(const std::string& pText)
{
    return static_cast<int>(std::strtol(pText.c_str(), nullptr, 10));
}

bool IsHexString(const std::string& pText, std::size_t pMin, std::size_t pMax = 0)
{
    if (pMax == 0) pMax = pMin;
    if (pText.size() < pMin || pText.size() > pMax) return false;
    for (char lChar : pText) {
        if (!((lChar >= '0' && lChar <= '9') || (lChar >= 'a' && lChar <= 'f'))) return false;
    }
    return true;
}

bool IsSafeShardName(const std::string& pText)
{
    if (pText.empty() || pText.size() > 64 || pText[0] == '.') return false;
    for (char lChar : pText) {
        const bool lOk = (lChar >= 'a' && lChar <= 'z') || (lChar >= 'A' && lChar <= 'Z') ||
                         (lChar >= '0' && lChar <= '9') || lChar == '-' || lChar == '_' || lChar == '.';
        if (!lOk) return false;
    }
    return true;
}

} // namespace

bool TrackCatalog::IsSafeTrackName(const std::string& pName)
{
    if (pName.empty() || pName.size() > kMaxTrackNameBytes || pName[0] == '.') {
        return false;
    }
    for (char lChar : pName) {
        const unsigned char lByte = static_cast<unsigned char>(lChar);
        if (lByte < 0x20 || lByte == 0x7f || lChar == '/' || lChar == '\\' || lChar == ':') {
            return false;
        }
    }
    return true;
}

bool TrackCatalog::ParseManifestLine(const std::string& pLine, TrackEntry& pOut)
{
    std::string lLine = pLine;
    while (!lLine.empty() && (lLine.back() == '\r' || lLine.back() == '\n')) {
        lLine.pop_back();
    }
    if (lLine.empty() || lLine[0] == '#') {
        return false;
    }
    const std::vector<std::string> lFields = SplitTabs(lLine);
    if (lFields.size() < 4 || !IsSafeTrackName(lFields[0])) {
        return false;
    }
    if (lFields[1] != "race" && lFields[1] != "freeplay") {
        return false;
    }
    pOut = TrackEntry();
    pOut.mName = lFields[0];
    pOut.mOfficial = false;
    pOut.mFreePlay = lFields[1] == "freeplay";
    pOut.mStarts = ToInt(lFields[2]);
    pOut.mRooms = ToInt(lFields[3]);

    // Optional download columns: bytes, sha256, shard, asset, gzip bytes.
    if (lFields.size() >= 10) {
        const std::string& lSha = lFields[6];
        const std::string& lShard = lFields[7];
        const std::string& lAsset = lFields[8];
        if (IsHexString(lSha, 64) && IsHexString(lAsset, 16, 64) && IsSafeShardName(lShard)) {
            pOut.mHasDownload = true;
            pOut.mBytes = std::strtoll(lFields[5].c_str(), nullptr, 10);
            pOut.mSha256 = lSha;
            pOut.mShard = lShard;
            pOut.mAsset = lAsset;
            pOut.mDownloadBytes = std::strtoll(lFields[9].c_str(), nullptr, 10);
        }
    }
    return true;
}

void TrackCatalog::Build(const TrackCatalogOptions& pOptions)
{
    mOptions = pOptions;
    mEntries.clear();

    std::set<std::string> lTaken;
    for (const std::string& lName : pOptions.mOfficialNames) {
        TrackEntry lEntry;
        lEntry.mName = lName;
        lEntry.mOfficial = true;
        lEntry.mPath = FindTrackFile(pOptions.mOfficialDirectories, lName);
        if (lEntry.mPath.empty() && !pOptions.mOfficialDirectories.empty()) {
            // Keep the entry (the official list is fixed) with the expected path,
            // so loading it reports a clear "could not load" instead of vanishing.
            lEntry.mPath = pOptions.mOfficialDirectories.front() + lName + ".trk";
        }
        lTaken.insert(Lower(lName));
        mEntries.push_back(lEntry);
    }
    mOfficialCount = static_cast<int>(mEntries.size());

    std::vector<TrackEntry> lCommunity;
    std::ifstream lManifest(pOptions.mManifestPath.c_str());
    std::string lLine;
    while (std::getline(lManifest, lLine)) {
        TrackEntry lEntry;
        if (!ParseManifestLine(lLine, lEntry)) {
            continue;
        }
        if (!lTaken.insert(Lower(lEntry.mName)).second) {
            continue; // duplicate of an official or earlier community track
        }
        lEntry.mPath = FindTrackFile(pOptions.mCommunityDirectories, lEntry.mName);
        lEntry.mInstalled = !lEntry.mPath.empty();
        if (!lEntry.mInstalled) {
            if (!lEntry.mHasDownload) {
                continue; // not installed and nowhere to get it from
            }
            // Listed so the player can pick it; it is downloaded when needed.
            lEntry.mPath = pOptions.mDownloadDirectory + lEntry.mName + ".trk";
        }
        lCommunity.push_back(lEntry);
    }

    std::sort(lCommunity.begin(), lCommunity.end(), [](const TrackEntry& pA, const TrackEntry& pB) {
        const std::string lA = Lower(pA.mName);
        const std::string lB = Lower(pB.mName);
        return lA != lB ? lA < lB : pA.mName < pB.mName;
    });
    mEntries.insert(mEntries.end(), lCommunity.begin(), lCommunity.end());
}

int TrackCatalog::Find(const std::string& pName) const
{
    for (int lIndex = 0; lIndex < Count(); ++lIndex) {
        if (mEntries[lIndex].mName == pName) {
            return lIndex;
        }
    }
    const std::string lWanted = Lower(pName);
    for (int lIndex = 0; lIndex < Count(); ++lIndex) {
        if (Lower(mEntries[lIndex].mName) == lWanted) {
            return lIndex;
        }
    }
    return -1;
}

void TrackCatalog::Refresh()
{
    for (TrackEntry& lEntry : mEntries) {
        if (lEntry.mOfficial) {
            continue;
        }
        const std::string lPath = FindTrackFile(mOptions.mCommunityDirectories, lEntry.mName);
        lEntry.mInstalled = !lPath.empty();
        if (lEntry.mInstalled) {
            lEntry.mPath = lPath;
        }
    }
}

std::vector<TrackEntry> TrackCatalog::MissingDownloads() const
{
    std::vector<TrackEntry> lMissing;
    for (const TrackEntry& lEntry : mEntries) {
        if (!lEntry.mOfficial && !lEntry.mInstalled && lEntry.mHasDownload) {
            lMissing.push_back(lEntry);
        }
    }
    return lMissing;
}

long long TrackCatalog::MissingDownloadBytes() const
{
    long long lTotal = 0;
    for (const TrackEntry& lEntry : MissingDownloads()) {
        lTotal += lEntry.mDownloadBytes;
    }
    return lTotal;
}

const TrackEntry* TrackCatalog::Get(int pIndex) const
{
    if (pIndex < 0 || pIndex >= Count()) {
        return nullptr;
    }
    return &mEntries[pIndex];
}

std::string TrackCatalog::PathFor(const std::string& pName) const
{
    const int lIndex = Find(pName);
    if (lIndex < 0 || !mEntries[lIndex].mInstalled) {
        return std::string();
    }
    return mEntries[lIndex].mPath;
}

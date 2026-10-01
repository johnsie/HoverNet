// TrackCatalog.cpp

#include "TrackCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>

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
    return true;
}

void TrackCatalog::Build(const TrackCatalogOptions& pOptions)
{
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
        if (lEntry.mPath.empty()) {
            continue; // the community pack isn't installed (or lacks this track)
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
    return lIndex < 0 ? std::string() : mEntries[lIndex].mPath;
}

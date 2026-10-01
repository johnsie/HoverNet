// TrackCatalog.h
//
// The one list of tracks every selector, preview and loader goes through.
//
// Official tracks (the ones bundled with the game) always come first, in a
// fixed order. Community tracks follow, sorted by name. A community track is
// listed only if it is named in the manifest AND its .trk file is present in one
// of the community directories, so a player without the (large, separately
// distributed) community pack simply sees the official tracks.
//
// Names are only ever resolved to files through the catalog. A name that isn't
// in it -- including anything a remote host sends -- never reaches the
// filesystem, so a hostile "../../" track name cannot escape the track folders.

#ifndef HOVERNET_TRACK_CATALOG_H
#define HOVERNET_TRACK_CATALOG_H

#include <string>
#include <vector>

struct TrackEntry
{
    std::string mName;
    std::string mPath;          // resolved .trk file (empty if unknown)
    bool mOfficial = true;
    bool mFreePlay = false;     // no complete finish/checkpoint set: there are no laps to complete
    int mStarts = 0;            // number of start slots (0 = not known)
    int mRooms = 0;
};

struct TrackCatalogOptions
{
    std::vector<std::string> mOfficialNames;        // fixed display order
    std::vector<std::string> mOfficialDirectories;  // searched in order, each ending in '/'
    std::string mManifestPath;                      // community manifest (may be missing)
    std::vector<std::string> mCommunityDirectories; // searched in order, each ending in '/'
};

class TrackCatalog
{
public:
    void Build(const TrackCatalogOptions& pOptions);

    const std::vector<TrackEntry>& Entries() const { return mEntries; }
    int Count() const { return static_cast<int>(mEntries.size()); }
    int OfficialCount() const { return mOfficialCount; }

    // Index of the track called pName, or -1. Exact match, then case-insensitive.
    int Find(const std::string& pName) const;
    const TrackEntry* Get(int pIndex) const;
    // File for pName, or an empty string if the catalog doesn't know it.
    std::string PathFor(const std::string& pName) const;

    // Parses one manifest line ("name<TAB>mode<TAB>starts<TAB>rooms[<TAB>...]").
    // Returns false for comments, blank lines and malformed or unsafe names.
    static bool ParseManifestLine(const std::string& pLine, TrackEntry& pOut);
    // True for a name that is safe to turn into a file name (no separators, no
    // control characters, not hidden, not too long for the network protocol).
    static bool IsSafeTrackName(const std::string& pName);

private:
    std::vector<TrackEntry> mEntries;
    int mOfficialCount = 0;
};

#endif

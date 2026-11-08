// TrackDownloader.h
//
// Downloads community tracks on demand. A track is hosted as a gzip file named
// after a hash (so awkward track names never appear in a URL):
//
//     <base>/<shard>/<asset>.trk.gz
//
// The manifest supplies <shard>, <asset>, the expected size and SHA-256 of the
// decompressed .trk. The download is made by the system's curl (or wget, or
// curl.exe on Windows) run as a child process WITHOUT a shell, then unpacked,
// verified and moved into place atomically, so a failed, cancelled or tampered
// download never leaves a half-written track behind.

#ifndef HOVERNET_TRACK_DOWNLOADER_H
#define HOVERNET_TRACK_DOWNLOADER_H

#include "TrackCatalog.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct TrackDownloadSettings
{
    std::string mBaseUrl;       // no trailing slash; https unless overridden for testing
    std::string mDestination;   // directory the tracks are installed into, ending in '/'
};

// The download location used unless HOVERNET_TRACK_DOWNLOAD_URL overrides it.
std::string DefaultTrackDownloadBaseUrl();

// Track Market (docs/specs/track-market.md): where its tracks (shard "market") are downloaded from, and its catalogue. HOVERNET_MARKET_DOWNLOAD_URL and
// HOVERNET_MARKET_CATALOGUE_URL override them (tests, or another market). In the browser both are on the page's own origin.
std::string DefaultMarketDownloadBaseUrl();
std::string DefaultMarketCatalogueUrl();
// Fetches the market catalogue into pDestination (atomically: a failed or invalid answer leaves the previous file alone). Blocking; run it on a thread.
// The answer must look like a catalogue and be small. Returns false with a short reason in pError.
bool FetchMarketCatalogue(const std::string& pDestination, std::string& pError);

// Creates pPath and any missing parents. Returns true if it exists afterwards.
bool MakeDirectories(const std::string& pPath);

// One track, blocking. pCancel may be set from another thread; pBytesDone is
// updated with the compressed bytes received so far. On failure pError holds a
// short message a player can act on.
bool DownloadTrack(const TrackEntry& pTrack, const TrackDownloadSettings& pSettings,
                   const std::atomic<bool>& pCancel, std::atomic<long long>& pBytesDone,
                   std::string& pError);

// Downloads a list of tracks on a background thread, one after another, so a UI
// can show progress and offer Cancel. Destroying the job cancels and joins it.
class TrackDownloadJob
{
public:
    TrackDownloadJob(const TrackDownloadSettings& pSettings, const std::vector<TrackEntry>& pTracks);
    ~TrackDownloadJob();

    void Start();
    void Cancel();
    // Runs to completion on the calling thread (command line / tests).
    void RunBlocking();

    bool Finished() const { return mFinished.load(); }
    bool Succeeded() const { return mFinished.load() && mError.empty() && !mCancelled.load(); }
    bool Cancelled() const { return mCancelled.load(); }
    int TotalTracks() const { return static_cast<int>(mTracks.size()); }
    int CompletedTracks() const { return mCompleted.load(); }
    long long TotalBytes() const { return mTotalBytes; }
    // Compressed bytes received across all tracks so far.
    long long BytesDone() const { return mFinishedBytes.load() + mCurrentBytes.load(); }
    std::string CurrentName() const;
    std::string Error() const;
    // Names of the tracks that were installed (so the catalog can be refreshed).
    std::vector<std::string> InstalledNames() const;

private:
    void Run();

    TrackDownloadSettings mSettings;
    std::vector<TrackEntry> mTracks;
    long long mTotalBytes = 0;

    std::thread mThread;
    std::atomic<bool> mFinished{false};
    std::atomic<bool> mCancelled{false};
    std::atomic<int> mCompleted{0};
    std::atomic<long long> mFinishedBytes{0};
    std::atomic<long long> mCurrentBytes{0};
    mutable std::mutex mMutex;
    std::string mCurrentName;
    std::string mError;
    std::vector<std::string> mInstalled;
};

#endif

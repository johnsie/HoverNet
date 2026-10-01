// Exercises Sha256, Gunzip and the track downloader (against a file:// "server",
// so no network is needed). The download cases need curl and are skipped, with a
// message, if it is not installed.

#include "Gunzip.h"
#include "Sha256.h"
#include "TrackCatalog.h"
#include "TrackDownloader.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

int gFailures = 0;

void Check(bool pCondition, const char* pMessage)
{
    if (!pCondition) {
        std::fprintf(stderr, "FAIL: %s\n", pMessage);
        ++gFailures;
    }
}

std::string ReadAll(const std::string& pPath)
{
    std::ifstream lFile(pPath.c_str(), std::ios::binary);
    std::ostringstream lText;
    lText << lFile.rdbuf();
    return lText.str();
}

void WriteAll(const std::string& pPath, const std::string& pData)
{
    std::ofstream lFile(pPath.c_str(), std::ios::binary | std::ios::trunc);
    lFile << pData;
}

bool Exists(const std::string& pPath)
{
    std::ifstream lFile(pPath.c_str(), std::ios::binary);
    return lFile.good();
}

bool Run(const std::string& pCommand)
{
    return std::system(pCommand.c_str()) == 0;
}

} // namespace

int main()
{
    // ---- SHA-256 known answers ----
    Check(Sha256::HashBytes("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256 of the empty string");
    Check(Sha256::HashBytes("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256 of abc");
    const std::string lLong = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Check(Sha256::HashBytes(lLong.data(), lLong.size()) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 across a block boundary");
    {
        Sha256 lHash;
        const std::string lChunk(1000, 'a');
        for (int lIndex = 0; lIndex < 1000; ++lIndex) lHash.Update(lChunk.data(), lChunk.size());
        Check(lHash.Finish() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "sha256 of one million 'a' fed in chunks");
    }

    char lTemplate[] = "/tmp/hovernet-download-XXXXXX";
    const char* lRoot = mkdtemp(lTemplate);
    if (lRoot == nullptr) {
        std::fprintf(stderr, "could not create a temp directory\n");
        return 1;
    }
    const std::string lBase = lRoot;

    // A "track": compressible and not trivially small.
    std::string lTrack;
    for (int lIndex = 0; lIndex < 20000; ++lIndex) {
        lTrack += "room " + std::to_string(lIndex % 97) + " wall " + std::to_string(lIndex * 7 % 1013) + "\n";
    }
    WriteAll(lBase + "/track.trk", lTrack);
    const bool lHaveGzip = Run("gzip -c '" + lBase + "/track.trk' > '" + lBase + "/track.trk.gz'");
    if (!lHaveGzip) {
        std::printf("gzip not available; skipping gunzip and download checks\n");
        return gFailures == 0 ? 0 : 1;
    }

    // ---- gunzip ----
    {
        std::string lError;
        Check(GunzipFile(lBase + "/track.trk.gz", lBase + "/out.trk", 1u << 24, lError), "gunzip a real gzip file");
        Check(ReadAll(lBase + "/out.trk") == lTrack, "gunzip output matches the original");

        Check(!GunzipFile(lBase + "/track.trk.gz", lBase + "/small.trk", 1000, lError),
              "gunzip refuses output over the limit");
        Check(lError.find("too large") != std::string::npos, "the limit error says so");

        std::string lCorrupt = ReadAll(lBase + "/track.trk.gz");
        lCorrupt[lCorrupt.size() / 2] ^= 0x55;
        WriteAll(lBase + "/corrupt.gz", lCorrupt);
        Check(!GunzipFile(lBase + "/corrupt.gz", lBase + "/c.trk", 1u << 24, lError), "gunzip rejects corrupt data");

        std::string lTruncated = ReadAll(lBase + "/track.trk.gz");
        lTruncated.resize(lTruncated.size() / 2);
        WriteAll(lBase + "/truncated.gz", lTruncated);
        Check(!GunzipFile(lBase + "/truncated.gz", lBase + "/t.trk", 1u << 24, lError), "gunzip rejects truncated data");

        WriteAll(lBase + "/notgzip.gz", "this is plainly not gzip data at all");
        Check(!GunzipFile(lBase + "/notgzip.gz", lBase + "/n.trk", 1u << 24, lError), "gunzip rejects non-gzip data");
    }

    // ---- downloader against file:// ----
    if (!Run("curl --version > /dev/null 2>&1")) {
        std::printf("curl not available; skipping download checks\n");
        return gFailures == 0 ? 0 : 1;
    }
    const std::string lSha = Sha256::HashBytes(lTrack.data(), lTrack.size());
    const std::string lAsset = lSha.substr(0, 16);
    mkdir((lBase + "/host").c_str(), 0755);
    mkdir((lBase + "/host/shard-1").c_str(), 0755);
    Run("cp '" + lBase + "/track.trk.gz' '" + lBase + "/host/shard-1/" + lAsset + ".trk.gz'");

    TrackEntry lEntry;
    lEntry.mName = "Test [1-2] Track";
    lEntry.mOfficial = false;
    lEntry.mInstalled = false;
    lEntry.mHasDownload = true;
    lEntry.mShard = "shard-1";
    lEntry.mAsset = lAsset;
    lEntry.mSha256 = lSha;
    lEntry.mBytes = static_cast<long long>(lTrack.size());
    lEntry.mDownloadBytes = static_cast<long long>(ReadAll(lBase + "/track.trk.gz").size());

    TrackDownloadSettings lSettings;
    lSettings.mBaseUrl = "file://" + lBase + "/host";
    lSettings.mDestination = lBase + "/dest/nested/";

    std::atomic<bool> lNoCancel{false};
    std::atomic<long long> lBytes{0};
    std::string lError;

    Check(DownloadTrack(lEntry, lSettings, lNoCancel, lBytes, lError), "download, verify and install a track");
    Check(ReadAll(lSettings.mDestination + lEntry.mName + ".trk") == lTrack, "installed file is the original");
    Check(!Exists(lSettings.mDestination + lAsset + ".part") && !Exists(lSettings.mDestination + lAsset + ".tmp"),
          "no temporary files are left behind");

    // Wrong hash: nothing is installed.
    TrackEntry lTampered = lEntry;
    lTampered.mName = "Tampered";
    lTampered.mSha256 = std::string(64, 'a');
    Check(!DownloadTrack(lTampered, lSettings, lNoCancel, lBytes, lError), "a hash mismatch is rejected");
    Check(lError.find("verification") != std::string::npos, "the mismatch error says so");
    Check(!Exists(lSettings.mDestination + "Tampered.trk"), "a rejected download installs nothing");

    // Missing file on the server.
    TrackEntry lMissing = lEntry;
    lMissing.mName = "Missing";
    lMissing.mAsset = "0123456789abcdef";
    Check(!DownloadTrack(lMissing, lSettings, lNoCancel, lBytes, lError), "a 404 fails");
    Check(lError.find("not available on the download server") != std::string::npos,
          "a missing file is reported as not published, not as a connection problem");
    Check(!Exists(lSettings.mDestination + "Missing.trk"), "a failed download installs nothing");

    // Entries that must never be fetched.
    TrackEntry lBad = lEntry;
    lBad.mShard = "../escape";
    Check(!DownloadTrack(lBad, lSettings, lNoCancel, lBytes, lError), "a path-like shard is refused");
    lBad = lEntry;
    lBad.mName = "../escape";
    Check(!DownloadTrack(lBad, lSettings, lNoCancel, lBytes, lError), "an unsafe track name is refused");
    lBad = lEntry;
    lBad.mAsset = "not hex!";
    Check(!DownloadTrack(lBad, lSettings, lNoCancel, lBytes, lError), "a non-hex asset name is refused");
    lBad = lEntry;
    lBad.mHasDownload = false;
    Check(!DownloadTrack(lBad, lSettings, lNoCancel, lBytes, lError), "a track with no download is refused");

    // Cancelled before it starts.
    std::atomic<bool> lCancelled{true};
    TrackEntry lCancel = lEntry;
    lCancel.mName = "Cancelled";
    TrackDownloadJob lCancelJob(lSettings, {lCancel});
    lCancelJob.Cancel();
    lCancelJob.RunBlocking();
    Check(lCancelJob.Cancelled() && !lCancelJob.Succeeded(), "a cancelled job is not a success");
    Check(!Exists(lSettings.mDestination + "Cancelled.trk"), "a cancelled job installs nothing");
    (void)lCancelled;

    // A job over several tracks, on its own thread, with one bad entry last.
    TrackEntry lSecond = lEntry;
    lSecond.mName = "Second";
    TrackDownloadJob lJob(lSettings, {lSecond, lMissing});
    lJob.Start();
    while (!lJob.Finished()) usleep(10000);
    Check(!lJob.Succeeded() && !lJob.Error().empty(), "a job with a failing track reports an error");
    Check(lJob.CompletedTracks() == 1 && lJob.InstalledNames().size() == 1 && lJob.InstalledNames()[0] == "Second",
          "tracks before the failure are kept and reported");
    Check(Exists(lSettings.mDestination + "Second.trk"), "the good track of the job is installed");

    // The catalog sees a downloaded track as installed after Refresh().
    {
        const std::string lManifestPath = lBase + "/manifest.tsv";
        std::ostringstream lLine;
        lLine << "Test [1-2] Track\trace\t10\t5\t0\t" << lEntry.mBytes << "\t" << lSha << "\tshard-1\t" << lAsset
              << "\t" << lEntry.mDownloadBytes << "\n";
        WriteAll(lManifestPath, lLine.str());
        TrackCatalogOptions lOptions;
        lOptions.mOfficialNames = {"Official"};
        lOptions.mOfficialDirectories = {lBase + "/none/"};
        lOptions.mManifestPath = lManifestPath;
        lOptions.mCommunityDirectories = {lBase + "/dest2/"};
        lOptions.mDownloadDirectory = lBase + "/dest2/";
        TrackCatalog lCatalog;
        lCatalog.Build(lOptions);
        Check(lCatalog.Count() == 2 && lCatalog.Get(1) != nullptr && !lCatalog.Get(1)->mInstalled &&
                  lCatalog.Get(1)->mHasDownload,
              "a downloadable track is listed although it is not installed");
        Check(lCatalog.PathFor("Test [1-2] Track").empty(), "an uninstalled track has no path to load");
        Check(lCatalog.MissingDownloads().size() == 1 && lCatalog.MissingDownloadBytes() == lEntry.mDownloadBytes,
              "the catalog counts what is missing");

        TrackDownloadSettings lCatalogSettings = lSettings;
        lCatalogSettings.mDestination = lBase + "/dest2/";
        TrackDownloadJob lCatalogJob(lCatalogSettings, lCatalog.MissingDownloads());
        lCatalogJob.RunBlocking();
        Check(lCatalogJob.Succeeded(), "downloading everything missing succeeds");
        lCatalog.Refresh();
        Check(lCatalog.Get(1)->mInstalled && lCatalog.PathFor("Test [1-2] Track") == lBase + "/dest2/Test [1-2] Track.trk",
              "after Refresh the downloaded track is installed");
        Check(lCatalog.MissingDownloads().empty(), "nothing is missing afterwards");
    }

    if (gFailures != 0) {
        std::fprintf(stderr, "%d download check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("Track download smoke passed\n");
    return 0;
}

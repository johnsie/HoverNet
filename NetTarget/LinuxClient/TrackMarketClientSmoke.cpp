// The game's side of the Track Market (docs/specs/track-market.md): reading the catalogue, merging it into the track list, fetching it, and downloading a
// market track from the market's own address. The "server" is a folder of files read through file:// URLs, so no network is needed (curl is required).

#include "Sha256.h"
#include "TrackCatalog.h"
#include "TrackDownloader.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int gFailures = 0;
void Check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++gFailures;
}
std::string ReadAll(const std::string& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream t;
    t << f.rdbuf();
    return t.str();
}
void WriteAll(const std::string& path, const std::string& data)
{
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    f << data;
}
bool Exists(const std::string& path) { std::ifstream f(path.c_str(), std::ios::binary); return f.good(); }
std::string Row(const std::string& name, const std::string& data, const char* author, const char* licence, const char* status = "listed")
{
    const std::string sha = Sha256::HashBytes(data.data(), data.size());
    return name + "\trace\t8\t24\t0\t" + std::to_string(data.size()) + "\t" + sha + "\tmarket\t" + sha + "\t60\t" + author + "\t" + licence + "\t2026-11-08\t1\t" + status + "\n";
}
}  // namespace

int main()
{
    char tmpl[] = "/tmp/hn-market-client-XXXXXX";
    const std::string work = mkdtemp(tmpl);
    const std::string pub = work + "/public";
    const std::string dl = work + "/downloads/";
    system(("mkdir -p '" + pub + "/market' '" + work + "/community' '" + dl + "'").c_str());

    // ---- one line of the catalogue
    const std::string dataA = "TRACK A BYTES", dataB = "TRACK B BYTES", dataC = "TRACK C BYTES";
    TrackEntry e;
    Check(TrackCatalog::ParseManifestLine(Row("Alpha Loop", dataA, "Alice", "CC-BY-4.0"), e) && e.mMarket && e.mAuthor == "Alice" && e.mLicence == "CC-BY-4.0" && e.mHasDownload && e.mShard == "market",
          "a market line is read with its author and licence");
    Check(!TrackCatalog::ParseManifestLine(Row("No Author", dataA, "-", "CC-BY-4.0"), e), "a market track without an author is not listed (attribution is part of the licence)");
    Check(!TrackCatalog::ParseManifestLine(Row("No Licence", dataA, "Alice", "-"), e), "a market track without a licence is not listed");
    Check(!TrackCatalog::ParseManifestLine("-\t-\t0\t0\t0\t0\t" + std::string(64, 'a') + "\tmarket\t" + std::string(64, 'a') + "\t0\t-\t-\t2026-11-08\t1\trevoked\n", e), "a revoked row is never listed");
    Check(TrackCatalog::ParseManifestLine("Old One\trace\t8\t24\t0\t10\t" + std::string(64, 'b') + "\tcommunity-tracks-1\t" + std::string(64, 'b') + "\t5\n", e) && !e.mMarket, "an ordinary community line is unchanged");

    // ---- the track list: official, community, then the market
    WriteAll(work + "/CommunityTracks.tsv", "Old One\trace\t8\t24\t0\t10\t" + std::string(64, 'b') + "\tcommunity-tracks-1\t" + std::string(64, 'b') + "\t5\n");
    WriteAll(work + "/catalogue.tsv",
             "# HoverNet Track Market catalogue\n# catalogue-version 3\n" + Row("Beta Loop", dataB, "Bob", "CC0-1.0") + Row("Alpha Loop", dataA, "Alice", "CC-BY-4.0") +
             Row("Old One", dataC, "Mallory", "CC0-1.0") + Row("Classic H", dataC, "Mallory", "CC0-1.0") + Row("Gone", dataC, "-", "-", "revoked"));
    TrackCatalogOptions options;
    options.mOfficialNames = {"Classic H"};
    options.mOfficialDirectories = {work + "/"};
    options.mManifestPath = work + "/CommunityTracks.tsv";
    options.mCommunityDirectories = {work + "/community/", dl};
    options.mDownloadDirectory = dl;
    options.mMarketCataloguePath = work + "/catalogue.tsv";
    TrackCatalog catalog;
    catalog.Build(options);
    Check(catalog.Count() == 4 && catalog.MarketStart() == 2, "the list is: 1 official, 1 community, then 2 market tracks");
    Check(catalog.Get(2) && catalog.Get(2)->mName == "Alpha Loop" && catalog.Get(3) && catalog.Get(3)->mName == "Beta Loop", "market tracks are sorted by name");
    Check(catalog.Find("Old One") == 1 && !catalog.Get(1)->mMarket, "a market track cannot take a community track's name");
    Check(catalog.Find("Gone") == -1, "a revoked track is not in the list");
    Check(catalog.Get(2)->mAuthor == "Alice" && !catalog.Get(2)->mInstalled, "a market track is listed as one to download, with its author");

    // ---- fetch the catalogue (file:// stands in for the web host)
    const std::string cache = work + "/cache.tsv";
    setenv("HOVERNET_MARKET_CATALOGUE_URL", ("file://" + work + "/catalogue.tsv").c_str(), 1);
    std::string error;
    Check(FetchMarketCatalogue(cache, error) && ReadAll(cache) == ReadAll(work + "/catalogue.tsv"), "the catalogue is fetched and saved");
    WriteAll(work + "/page.html", "<html>Sign in to the hotel wifi</html>");
    setenv("HOVERNET_MARKET_CATALOGUE_URL", ("file://" + work + "/page.html").c_str(), 1);
    const std::string before = ReadAll(cache);
    Check(!FetchMarketCatalogue(cache, error) && ReadAll(cache) == before && !Exists(cache + ".part"), "an answer that is not a catalogue is refused and the last good one is kept");
    setenv("HOVERNET_MARKET_CATALOGUE_URL", ("file://" + work + "/nothing-here.tsv").c_str(), 1);
    Check(!FetchMarketCatalogue(cache, error) && ReadAll(cache) == before, "an unreachable market leaves the last good catalogue alone");

    // ---- download a market track from the market's address (not the community base)
    system(("printf '%s' '" + dataA + "' | gzip -9n > '" + pub + "/market/" + Sha256::HashBytes(dataA.data(), dataA.size()) + ".trk.gz'").c_str());
    setenv("HOVERNET_MARKET_DOWNLOAD_URL", ("file://" + pub).c_str(), 1);
    TrackDownloadSettings settings;
    settings.mBaseUrl = "file://" + work + "/nowhere";     // the community location: must not be used for a market track
    settings.mDestination = dl;
    std::atomic<bool> noCancel{false};
    std::atomic<long long> bytes{0};
    Check(DownloadTrack(*catalog.Get(2), settings, noCancel, bytes, error) && ReadAll(dl + "Alpha Loop.trk") == dataA, "a market track downloads from the market's address and is installed under its name");
    // a file that does not match the catalogue is refused
    system(("printf '%s' 'SOMETHING ELSE ENTIRELY' | gzip -9n > '" + pub + "/market/" + Sha256::HashBytes(dataB.data(), dataB.size()) + ".trk.gz'").c_str());
    Check(!DownloadTrack(*catalog.Get(3), settings, noCancel, bytes, error) && !Exists(dl + "Beta Loop.trk"), "a download that does not match the catalogue is refused and nothing is installed");
    catalog.Refresh();
    Check(catalog.Get(2)->mInstalled && !catalog.Get(3)->mInstalled, "after a download the list shows what is installed");

    // ---- a revoked track leaves the machine too; one the author merely unlisted stays
    Check(Exists(dl + ".market-installed") && ReadAll(dl + ".market-installed").find(Sha256::HashBytes(dataA.data(), dataA.size())) != std::string::npos, "an installed market track is recorded with its hash");
    WriteAll(dl + "Unlisted One.trk", dataB);
    { std::ofstream r((dl + ".market-installed").c_str(), std::ios::app); r << "Unlisted One\t" << Sha256::HashBytes(dataB.data(), dataB.size()) << "\n"; }
    WriteAll(work + "/cat-revoked.tsv", "# HoverNet Track Market catalogue\n# catalogue-version 4\n" + Row("Beta Loop", dataB, "Bob", "CC0-1.0") +
             "-\t-\t0\t0\t0\t0\t" + Sha256::HashBytes(dataA.data(), dataA.size()) + "\tmarket\t" + Sha256::HashBytes(dataA.data(), dataA.size()) + "\t0\t-\t-\t2026-11-09\t1\trevoked\n");
    Check(RemoveRevokedMarketTracks(work + "/cat-revoked.tsv", dl) == 1 && !Exists(dl + "Alpha Loop.trk"), "a revoked track is deleted from the player's folder");
    Check(Exists(dl + "Unlisted One.trk"), "a track that is merely no longer listed stays on the machine");
    Check(RemoveRevokedMarketTracks(work + "/cat-revoked.tsv", dl) == 0, "revoking twice removes nothing more");
    WriteAll(dl + "Edited.trk", "the player made their own changes");
    { std::ofstream r((dl + ".market-installed").c_str(), std::ios::app); r << "Edited\t" << Sha256::HashBytes(dataA.data(), dataA.size()) << "\n"; }
    Check(RemoveRevokedMarketTracks(work + "/cat-revoked.tsv", dl) == 0 && Exists(dl + "Edited.trk"), "a file whose contents no longer match the revoked hash is not deleted");

    system(("rm -rf '" + work + "\'").c_str());
    if (gFailures == 0) std::puts("Track Market client checks passed");
    return gFailures == 0 ? 0 : 1;
}

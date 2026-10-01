// Exercises TrackCatalog against a throw-away directory tree: official tracks
// stay first and in their fixed order, community tracks are listed only when
// both in the manifest and on disk, and no hostile name reaches the filesystem.

#include "TrackCatalog.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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

void Touch(const std::string& pPath)
{
    std::ofstream lFile(pPath.c_str(), std::ios::binary);
    lFile << "x";
}

} // namespace

int main()
{
    char lTemplate[] = "/tmp/hovernet-catalog-XXXXXX";
    const char* lRoot = mkdtemp(lTemplate);
    if (lRoot == nullptr) {
        std::fprintf(stderr, "could not create a temp directory\n");
        return 1;
    }
    const std::string lBase = lRoot;
    const std::string lOfficial = lBase + "/official/";
    const std::string lCommunity = lBase + "/community/";
    mkdir((lBase + "/official").c_str(), 0755);
    mkdir((lBase + "/community").c_str(), 0755);

    Touch(lOfficial + "Alpha.trk");
    Touch(lOfficial + "Beta.trk");
    Touch(lCommunity + "zebra [1-9].trk");
    Touch(lCommunity + "Apple.trk");
    Touch(lCommunity + "banana.trk");
    Touch(lCommunity + "alpha.trk");        // clashes with the official Alpha
    Touch(lCommunity + "Arena.trk");

    const std::string lManifestPath = lBase + "/manifest.tsv";
    {
        std::ofstream lManifest(lManifestPath.c_str());
        lManifest << "# comment line\n"
                  << "\n"
                  << "zebra [1-9]\trace\t8\t40\t0\n"
                  << "Apple\trace\t10\t12\t0\n"
                  << "banana\tfreeplay\t4\t9\t3\n"
                  << "alpha\trace\t2\t5\t0\n"          // duplicate of official (case-insensitive)
                  << "Missing\trace\t2\t5\t0\n"         // not on disk
                  << "../../etc/passwd\trace\t2\t5\t0\n" // hostile name
                  << "sub/dir\trace\t2\t5\t0\n"
                  << "Arena\tbogusmode\t2\t5\t0\n"      // unknown mode
                  << "short\trace\n";                   // too few fields
    }

    TrackCatalogOptions lOptions;
    lOptions.mOfficialNames = {"Alpha", "Beta"};
    lOptions.mOfficialDirectories = {lOfficial};
    lOptions.mManifestPath = lManifestPath;
    lOptions.mCommunityDirectories = {lCommunity};

    TrackCatalog lCatalog;
    lCatalog.Build(lOptions);

    Check(lCatalog.OfficialCount() == 2, "two official tracks");
    Check(lCatalog.Count() == 5, "two official + three valid community tracks");
    Check(lCatalog.Get(0) && lCatalog.Get(0)->mName == "Alpha" && lCatalog.Get(0)->mOfficial,
          "official Alpha is first");
    Check(lCatalog.Get(1) && lCatalog.Get(1)->mName == "Beta" && lCatalog.Get(1)->mOfficial,
          "official Beta is second");
    Check(lCatalog.Get(2) && lCatalog.Get(2)->mName == "Apple" && !lCatalog.Get(2)->mOfficial,
          "community tracks start after the official ones, sorted case-insensitively (Apple)");
    Check(lCatalog.Get(3) && lCatalog.Get(3)->mName == "banana", "community sort (banana)");
    Check(lCatalog.Get(4) && lCatalog.Get(4)->mName == "zebra [1-9]", "community sort (zebra)");
    Check(lCatalog.Get(3) && lCatalog.Get(3)->mFreePlay && lCatalog.Get(3)->mStarts == 4 &&
              lCatalog.Get(3)->mRooms == 9,
          "manifest metadata is carried through");
    Check(lCatalog.Get(2) && !lCatalog.Get(2)->mFreePlay && lCatalog.Get(2)->mStarts == 10,
          "race mode and starts");
    Check(lCatalog.Find("Missing") < 0, "a manifest track with no file is not listed");
    Check(lCatalog.Find("Arena") < 0, "a manifest line with an unknown mode is ignored");
    Check(lCatalog.Find("alpha") == 0, "case-insensitive lookup finds the official track");
    Check(lCatalog.PathFor("Apple") == lCommunity + "Apple.trk", "community path resolves");
    Check(lCatalog.PathFor("Alpha") == lOfficial + "Alpha.trk", "official path resolves");
    Check(lCatalog.PathFor("../../etc/passwd").empty(), "hostile name resolves to nothing");
    Check(lCatalog.PathFor("sub/dir").empty(), "separator name resolves to nothing");
    Check(lCatalog.PathFor("not a track").empty(), "unknown name resolves to nothing");

    Check(!TrackCatalog::IsSafeTrackName(""), "empty name is unsafe");
    Check(!TrackCatalog::IsSafeTrackName(".hidden"), "dot name is unsafe");
    Check(!TrackCatalog::IsSafeTrackName("a\\b"), "backslash is unsafe");
    Check(!TrackCatalog::IsSafeTrackName("C:evil"), "drive colon is unsafe");
    Check(!TrackCatalog::IsSafeTrackName(std::string(64, 'x')), "over-long name is unsafe");
    Check(TrackCatalog::IsSafeTrackName("Cheryl&Franko's SockHop[1-848]"), "ordinary community name is safe");
    Check(TrackCatalog::IsSafeTrackName("Khazad-D\xC3\xBBm[1-279]"), "UTF-8 name is safe");

    // With no community pack installed only the official tracks remain.
    TrackCatalogOptions lNoPack = lOptions;
    lNoPack.mCommunityDirectories = {lBase + "/absent/"};
    TrackCatalog lOfficialOnly;
    lOfficialOnly.Build(lNoPack);
    Check(lOfficialOnly.Count() == 2 && lOfficialOnly.OfficialCount() == 2,
          "without the pack only official tracks are listed");

    // With no manifest at all the catalog still works.
    TrackCatalogOptions lNoManifest = lOptions;
    lNoManifest.mManifestPath = lBase + "/nope.tsv";
    TrackCatalog lNoManifestCatalog;
    lNoManifestCatalog.Build(lNoManifest);
    Check(lNoManifestCatalog.Count() == 2, "a missing manifest leaves the official tracks");

    if (gFailures != 0) {
        std::fprintf(stderr, "%d catalog check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("Track catalog smoke passed\n");
    return 0;
}

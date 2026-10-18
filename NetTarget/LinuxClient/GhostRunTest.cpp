// Unit test of GhostRun: recording (spacing, restart), lookup with interpolation and the short way round an angle, file names that cannot
// name another path, saving and loading, and what a damaged or hostile file may do (be ignored).
#include "GhostRun.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

int main()
{
    const char* path = "hovernet-ghost-test.tmp";

    GhostRun run;
    Check(run.Empty(), "starts empty");
    run.Record(-500, 1, 1, 1, 0, 0);
    Check(run.Empty(), "times before the start are not recorded");
    run.Record(0, 1000, 2000, 300, 4000, 5);
    run.Record(10, 9, 9, 9, 0, 0);
    run.Record(47, 9, 9, 9, 0, 0);
    Check(run.samples.size() == 1, "points closer than the interval are skipped");
    run.Record(48, 1480, 2480, 300, 100, 5);
    run.Record(96, 1960, 2960, 300, 100, 6);
    Check(run.samples.size() == 3, "a point at the interval is kept");

    GhostRun::Sample s;
    Check(!run.At(-1, s), "before the start there is no ghost");
    Check(run.At(0, s) && s.x == 1000 && s.y == 2000, "at the start it is on the first point");
    Check(run.At(24, s) && s.x == 1240 && s.y == 2240 && s.z == 300, "halfway between two points it is halfway");
    Check(run.At(24, s) && s.orientation == 2, "the angle goes the short way round 4000 to 100 (through 0)");
    Check(run.At(72, s) && s.x == 1720 && s.room == 5, "later points interpolate too and keep the earlier room");
    Check(run.At(96, s) && s.x == 1960 && s.room == 6, "the last point");
    Check(run.At(1096, s) && s.x == 1960, "just after the end it waits on the finish");
    Check(!run.At(1097, s), "a second after the end it is gone");

    run.Record(50, 0, 0, 0, 0, 0);   // time goes backwards: a restarted race
    Check(run.samples.size() == 1 && run.samples[0].t == 50, "a time that goes backwards starts the run again");

    GhostRun full;
    full.track = "Metro Spiral"; full.laps = 3; full.finishMs = 83456;
    for (int i = 0; i < 400; ++i) full.Record(i * 48, i * 10, -i * 20, 700, (i * 37) % 4096, i / 50);
    Check(full.Save(path), "saves");
    GhostRun loaded;
    Check(loaded.Load(path) && loaded.track == "Metro Spiral" && loaded.laps == 3 && loaded.finishMs == 83456 && loaded.samples.size() == 400, "loads what was saved");
    bool same = true;
    for (std::size_t i = 0; i < 400; ++i) {
        const GhostRun::Sample &a = full.samples[i], &b = loaded.samples[i];
        same = same && a.t == b.t && a.x == b.x && a.y == b.y && a.z == b.z && a.orientation == b.orientation && a.room == b.room;
    }
    Check(same, "every point comes back exactly");
    GhostRun empty;
    Check(!empty.Save(path), "an empty ghost is not saved");

    // Damaged files are ignored.
    std::vector<char> bytes;
    { std::ifstream in(path, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
    auto write = [&](const std::vector<char>& b) { std::ofstream out(path, std::ios::binary | std::ios::trunc); out.write(b.data(), static_cast<std::streamsize>(b.size())); };
    GhostRun bad;
    write(std::vector<char>(bytes.begin(), bytes.end() - 5));
    Check(!bad.Load(path) && bad.Empty(), "a truncated file is ignored");
    std::vector<char> wrongMagic = bytes; wrongMagic[0] = 'X';
    write(wrongMagic);
    Check(!bad.Load(path), "a file with another header is ignored");
    std::vector<char> hugeCount = bytes;   // the sample count sits after magic(8) + laps(4) + finish(4) + name length(2) + name(12)
    hugeCount[8 + 4 + 4 + 2 + 12 + 3] = static_cast<char>(0x7f);
    write(hugeCount);
    Check(!bad.Load(path), "a sample count larger than the file is ignored");
    std::vector<char> notIncreasing = bytes;   // make the second sample's time equal to the first's
    const std::size_t firstSample = 8 + 4 + 4 + 2 + 12 + 4;
    for (int i = 0; i < 4; ++i) notIncreasing[firstSample + 20 + static_cast<std::size_t>(i)] = notIncreasing[firstSample + static_cast<std::size_t>(i)];
    write(notIncreasing);
    Check(!bad.Load(path), "times that do not increase are ignored");
    write(std::vector<char>());
    Check(!bad.Load(path), "an empty file is ignored");
    std::remove(path);
    Check(!bad.Load("does-not-exist.ghost"), "a missing file is no ghost");

    Check(GhostRun::FileName("Metro Spiral", 3) == "ghost_Metro_Spiral_3.bin", "a file name from the track and laps");
    const std::string hostile = GhostRun::FileName("../../etc/passwd", 1);
    Check(hostile.find('/') == std::string::npos && hostile.find('\\') == std::string::npos && hostile.substr(0, 6) == "ghost_", "a track name cannot name another path");
    Check(GhostRun::FileName("a/b\\c", 2).find_first_of("/\\") == std::string::npos, "no path separators survive");
    Check(GhostRun::FileName("", 1).substr(0, 6) == "ghost_", "an empty name still gives a file name");

    if (gFailures == 0) std::printf("ghost run tests passed\n");
    return gFailures == 0 ? 0 : 1;
}

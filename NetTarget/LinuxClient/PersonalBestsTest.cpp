// Unit test of PersonalBests: first finish, improvement, a slower finish, per-lap-count race bests, ignoring non-times, saving and loading,
// and what a damaged or hostile file may do (nothing worse than being ignored).
#include "PersonalBests.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

int main()
{
    const char* path = "hovernet-personal-bests-test.tmp";   // in the working directory: portable, and removed at the end

    PersonalBests bests;
    Check(bests.Empty(), "starts empty");
    PersonalBests::Outcome o = bests.Record("ClassicH", 3, 95000, 31000);
    Check(o.newRaceBest && o.newLapBest && !o.hadRaceBest && !o.hadLapBest, "the first finish is a best, with nothing to compare with");
    o = bests.Record("ClassicH", 3, 97000, 30500);
    Check(!o.newRaceBest && o.hadRaceBest && o.previousRace == 95000, "a slower race is not a best and reports the old one");
    Check(o.newLapBest && o.previousLap == 31000, "a faster lap in a slower race is still a lap best");
    o = bests.Record("ClassicH", 3, 94000, 30500);
    Check(o.newRaceBest && o.previousRace == 95000 && !o.newLapBest, "a faster race is a best; an equal lap is not");
    o = bests.Record("ClassicH", 5, 150000, 30500);
    Check(o.newRaceBest && !o.hadRaceBest, "a race over a different number of laps is compared on its own");
    Check(bests.RaceBest("ClassicH", 3) == 94000 && bests.RaceBest("ClassicH", 5) == 150000 && bests.LapBest("ClassicH") == 30500, "the stored bests");
    o = bests.Record("ClassicH", 3, 0, 0);
    Check(!o.newRaceBest && !o.newLapBest && bests.RaceBest("ClassicH", 3) == 94000, "a time of zero is not a time");
    o = bests.Record("ClassicH", 3, -5, -5);
    Check(!o.newRaceBest && !o.newLapBest, "a negative time is not a time");
    o = bests.Record("", 3, 1000, 1000);
    Check(!o.newRaceBest && !o.newLapBest, "no track name, no record");
    o = bests.Record("Bad\tName", 3, 1000, 1000);
    Check(!o.newRaceBest && !o.newLapBest, "a track name that would break the file is refused");

    Check(bests.Save(path), "saves");
    PersonalBests loaded;
    loaded.Load(path);
    Check(loaded.RaceBest("ClassicH", 3) == 94000 && loaded.RaceBest("ClassicH", 5) == 150000 && loaded.LapBest("ClassicH") == 30500, "loads what was saved");

    // A damaged file: junk lines, a duplicate with a slower time, a huge number, a missing field, an unknown record type, CRLF line ends.
    std::FILE* f = std::fopen(path, "w");
    std::fputs("garbage\n\nR\tA\t3\t50000\nR\tA\t3\t60000\nR\tB\t3\t99999999999999999999\nR\tC\t3\nX\tD\t1\t5\nL\tA\t20000\r\nR\tE\t0\t1000\nR\tF\t3\t-4\n", f);
    std::fclose(f);
    PersonalBests damaged;
    damaged.Load(path);
    Check(damaged.RaceBest("A", 3) == 50000, "the fastest of duplicate lines wins");
    Check(damaged.LapBest("A") == 20000, "a line ending in CRLF still reads");
    Check(damaged.RaceBest("B", 3) == 0 && damaged.RaceBest("C", 3) == 0 && damaged.RaceBest("D", 1) == 0 && damaged.RaceBest("E", 0) == 0 && damaged.RaceBest("F", 3) == 0,
          "huge, short, unknown, zero-lap and negative records are ignored");

    PersonalBests missing;
    missing.Load("/nonexistent-directory/none");
    Check(missing.Empty(), "a missing file is empty");
    Check(!missing.Save("/nonexistent-directory/none"), "saving where nothing can be written reports failure");
    bests.Clear();
    Check(bests.Empty(), "clear forgets everything");
    std::remove(path);

    std::printf("%s\n", gFailures == 0 ? "personal bests: records, per-lap-count bests, save and load, and damaged files" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}

// Tests of the monthly league (Authority/League.*): the points, the rules that stop a player scoring from races against themselves, monthly and yearly
// winners, the reset on the 1st of the month, and that the log survives a restart.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../Authority/League.h"

using namespace Authority;

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

static std::int64_t At(int year, int month, int day, int hour = 12)
{
    std::tm t{};
    t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day; t.tm_hour = hour;
#ifdef _WIN32
    return static_cast<std::int64_t>(_mkgmtime(&t));
#else
    return static_cast<std::int64_t>(timegm(&t));
#endif
}

static LeagueEntrant Player(const std::string& name, int device, int rank, bool finished = true)
{
    LeagueEntrant e;
    e.name = name; e.hasDevice = true; e.deviceHigh = 0xABCD; e.deviceLow = static_cast<std::uint64_t>(device);
    e.finished = finished; e.finishMs = finished ? 60000 + rank * 1000 : 0; e.rank = rank;
    return e;
}

int main()
{
    const std::int64_t oct = At(2026, 10, 18);

    // Points: 3 for the winner, nothing for the others.
    {
        League league;
        const LeagueDecision d = league.Record(oct, {Player("Anna", 1, 1), Player("Ben", 2, 2), Player("Cara", 3, 3), Player("Dee", 4, 4)});
        Check(d.valid && d.scored && d.points == std::vector<int>({3, 0, 0, 0}), "the winner gets 3 points and nobody else gets any (second and third do not score)");
        const Protocol3::LeagueData data = league.Data("", oct);
        Check(data.currentMonth == "2026-10" && data.month == "2026-10" && data.standings.size() == 4 && data.standings[0].name == "Anna" && data.standings[0].points == 3 && data.standings[0].wins == 1 && data.standings[3].points == 0 && data.standings[3].races == 1, "the standings list everyone who finished, best first");
        // Points add up across races, and the order is by points.
        league.Record(oct + 100, {Player("Anna", 1, 2), Player("Ben", 2, 1)});
        const Protocol3::LeagueData next = league.Data("", oct + 200);
        Check(next.standings[0].name == "Anna" && next.standings[0].points == 3 && next.standings[1].name == "Ben" && next.standings[1].points == 3 && next.standings[1].wins == 1 && next.standings[0].wins == 1, "points add up; equal points are split by wins, then by who scored first");
    }

    // A race against yourself does not count.
    {
        League league;
        Check(!league.Record(oct, {Player("Anna", 1, 1), Player("Anna the second", 1, 2)}).valid, "two players on one device (two tabs, two windows) score for nobody");
        Check(!league.Record(oct, {Player("Anna", 1, 1), Player("anna", 2, 2)}).valid, "two players with the same name (any case) score for nobody");
        LeagueEntrant old = Player("Ben", 2, 2); old.hasDevice = false;
        Check(!league.Record(oct, {Player("Anna", 1, 1), old}).valid, "a player whose game sent no device id (an older game) makes the race not count");
        Check(!league.Record(oct, {Player("Anna", 1, 1)}).valid, "a race with one player never counts");
        Check(league.Data("", oct).standings.empty() && league.ScoredRaces() == 0, "and nothing was recorded");
        std::string reason;
        Check(!League::RaceIsValid({Player("Anna", 1, 1), Player("Ben", 1, 2)}, &reason) && reason.find("same device") != std::string::npos, "the reason is given");
    }

    // Two players must finish.
    {
        League league;
        const LeagueDecision d = league.Record(oct, {Player("Anna", 1, 1), Player("Ben", 2, 2, false)});
        Check(d.valid && !d.scored && d.points == std::vector<int>({0, 0}), "a race in which only one player finished is valid for the winners list but scores nothing (the other did not compete)");
    }

    // The same two devices score at most three duels in a UTC day.
    {
        League league;
        int scored = 0;
        for (int i = 0; i < 6; ++i) scored += league.Record(oct + i * 600, {Player("Anna", 1, 1), Player("Ben", 2, 2)}).scored ? 1 : 0;
        Check(scored == 3, "the same two devices score three duels a day and no more");
        Check(league.Record(At(2026, 10, 19, 1), {Player("Anna", 1, 1), Player("Ben", 2, 2)}).scored, "the next day they may score again");
        Check(league.Record(oct + 9000, {Player("Anna", 1, 1), Player("Ben", 2, 2), Player("Cara", 3, 3)}).scored, "a race with three devices is not a duel and always scores");
        Check(league.Record(oct + 9100, {Player("Anna", 1, 1), Player("Cara", 3, 2)}).scored, "a duel against someone else scores");
    }

    // Months and years: the league starts again on the 1st; winners are kept.
    {
        League league;
        league.Record(At(2025, 12, 20), {Player("Zed", 9, 1), Player("Yan", 8, 2)});
        league.Record(At(2026, 1, 31, 23), {Player("Anna", 1, 1), Player("Ben", 2, 2)});
        league.Record(At(2026, 2, 1, 0), {Player("Ben", 2, 1), Player("Anna", 1, 2)});
        const Protocol3::LeagueData feb = league.Data("", At(2026, 2, 1, 1));
        Check(feb.currentMonth == "2026-02" && feb.standings.size() == 2 && feb.standings[0].name == "Ben" && feb.standings[0].points == 3, "on the 1st the month starts again from nothing (a race at midnight on the 1st is February's)");
        Check(feb.pastWinners.size() == 2 && feb.pastWinners[0].month == "2026-01" && feb.pastWinners[0].name == "Anna" && feb.pastWinners[1].month == "2025-12" && feb.pastWinners[1].name == "Zed", "the winner of each finished month is kept, newest first");
        const Protocol3::LeagueData jan = league.Data("2026-01", At(2026, 2, 1, 1));
        Check(jan.month == "2026-01" && jan.standings.size() == 2 && jan.standings[0].name == "Anna", "a past month can be looked at");
        Check(feb.currentYear == 2026 && feb.yearStandings.size() == 2 && feb.yearStandings[0].points == 3 && feb.yearStandings[1].points == 3 && feb.yearStandings[0].name == "Anna", "the year so far adds the months (here a tie, which goes to whoever scored first)");
        Check(feb.yearWinners.size() == 1 && feb.yearWinners[0].year == 2025 && feb.yearWinners[0].name == "Zed", "the winner of a finished year is kept");
        const Protocol3::LeagueData later = league.Data("", At(2027, 1, 2));
        Check(later.yearWinners.size() == 2 && later.yearWinners[0].year == 2026 && later.yearWinners[0].name == "Anna" && later.yearWinners[0].points == 3, "when the year ends it has its winner");
        Check(later.standings.empty() && later.yearStandings.empty(), "and the new month and year start empty");
    }

    // A name change: points stay with the device, the latest name is shown.
    {
        League league;
        league.Record(oct, {Player("Old name", 1, 1), Player("Ben", 2, 2)});
        league.Record(oct + 5000, {Player("New name", 1, 1), Player("Cara", 3, 2)});
        const Protocol3::LeagueData data = league.Data("", oct + 6000);
        Check(data.standings[0].name == "New name" && data.standings[0].points == 6 && data.standings.size() == 3, "points belong to the device, whatever name it races under, and the latest name is shown");
    }

    // The log survives a restart, including the duel count.
    {
        const std::string path = "hovernet-league-test.tmp";
        std::remove(path.c_str());
        {
            League league(path);
            for (int i = 0; i < 3; ++i) league.Record(oct + i * 600, {Player("Anna", 1, 1), Player("Ben", 2, 2)});
        }
        League again(path);
        const Protocol3::LeagueData data = again.Data("", oct + 5000);
        Check(data.standings.size() == 2 && data.standings[0].points == 9 && again.ScoredRaces() == 3, "a restarted server has the same league");
        Check(!again.Record(oct + 4000, {Player("Anna", 1, 1), Player("Ben", 2, 2)}).scored, "and remembers how many duels have been scored today");
        std::FILE* f = std::fopen(path.c_str(), "a"); std::fputs("garbage line\nS\tnot\tenough\n", f); std::fclose(f);
        League damaged(path);
        Check(damaged.Data("", oct + 5000).standings.size() == 2, "lines that do not parse are ignored");
        std::remove(path.c_str());
    }

    // Protocol round trip of the answer, and the limits.
    {
        League league;
        for (int i = 0; i < 14; ++i) league.Record(At(2025, 1 + (i % 12), 5), {Player("P" + std::to_string(i), 100 + i, 1), Player("Q" + std::to_string(i), 200 + i, 2)});
        const Protocol3::LeagueData data = league.Data("", At(2026, 5, 5));
        Protocol3::LeagueData back;
        Check(Protocol3::Decode(Protocol3::Encode(data), back) && back.pastWinners.size() == data.pastWinners.size() && back.yearWinners.size() == data.yearWinners.size(), "the answer encodes and decodes");
        Check(data.pastWinners.size() <= static_cast<std::size_t>(Protocol3::kMaxLeagueMonths), "at most twelve past months are sent");
        Protocol3::ListLeague ask; ask.month = "2026-13";
        Protocol3::ListLeague parsed;
        Check(!Protocol3::Decode(Protocol3::Encode(ask), parsed), "a month that is not a month is refused");
        ask.month = "2026-02";
        Check(Protocol3::Decode(Protocol3::Encode(ask), parsed) && parsed.month == "2026-02", "a month is accepted");
    }

    if (gFailures == 0) std::printf("league tests passed\n");
    return gFailures == 0 ? 0 : 1;
}

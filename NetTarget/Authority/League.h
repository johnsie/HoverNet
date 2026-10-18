// League.h -- the monthly league of the HoverNet server: points for the top three finishers of races that count, standings per month, the winner of each
// finished month and of each finished year, kept in a small append-only log that survives restarts. No sockets, no clock of its own (times are passed in),
// so it is tested on its own (LeagueTest.cpp).
//
// POINTS: 3 for the winner of a race and nothing for the others (points for second and third would reward starting many two-player races). A race scores only if
//   - at least two players FINISHED it (a player who stays on the grid or leaves is not a competitor), and
//   - the players are on different devices: every player sent a device id (a random number its game made once and keeps, see kCapDeviceId), no two players
//     have the same one, and no two have the same display name. Someone racing themselves from two browser tabs, two windows or a copy of the game on the same
//     machine shares a device id (or, trying to hide it, a name) and the race scores for nobody; and
//   - a duel (a race with exactly two devices in it) between the same two devices scores at most three times in one UTC day, so two accounts of one person
//     cannot farm points from each other.
// IP addresses are never used (players share them). WHAT THIS CANNOT DO: a device id is a number the player's own game chooses, so someone who edits it by
// hand can pose as another device, and a person with two different computers is two devices. Only accounts (Stage 6a) can prove one person is one player; the
// rules above make cheating cost effort and keep the daily gain small, they do not make it impossible.
//
// The league's points belong to the device, the name shown is the one it used last. The month is the UTC calendar month and starts again on the 1st at 00:00 UTC.
#ifndef LEAGUE_H
#define LEAGUE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Protocol3.h"

namespace Authority
{
struct LeagueEntrant
{
    std::string name;
    bool hasDevice = false;
    std::uint64_t deviceHigh = 0, deviceLow = 0;
    bool finished = false;
    std::int32_t finishMs = 0;     // the race time when finished
    int rank = 0;                  // the race's own ranking (1 = first), finishers before the others
};

struct LeagueDecision
{
    bool valid = false;            // the players are on different devices, nobody races themselves (also what lists "recent winners")
    bool scored = false;           // points were given
    std::string reason;            // why not, in a few words (for the log of the server)
    std::vector<int> points;       // per entrant, in the order given
};

class League
{
public:
    // path: the log file (created when needed); empty keeps everything in memory. An existing log is read; lines that do not parse are ignored.
    explicit League(const std::string& path = std::string());

    // Decides whether a race that ended at `unixTime` counts, and if it does records its points. The same race must not be recorded twice.
    LeagueDecision Record(std::int64_t unixTime, const std::vector<LeagueEntrant>& entrants);
    // The validity check on its own (no points, nothing recorded): at least two players, all with different devices and different names.
    static bool RaceIsValid(const std::vector<LeagueEntrant>& entrants, std::string* reason = nullptr);

    // The league as the lobby sends it: the standings of `month` ("YYYY-MM", empty = the current one) and the history.
    Protocol3::LeagueData Data(const std::string& month, std::int64_t now) const;
    static std::string MonthOf(std::int64_t unixTime);       // "2026-10"
    static std::string DeviceKey(std::uint64_t high, std::uint64_t low);

    std::size_t ScoredRaces() const { return mRaces; }

private:
    struct Row { std::int64_t time = 0; std::string device, name; int points = 0, place = 0; };
    void Append(const std::string& line);
    void Add(const Row& row);
    std::vector<Protocol3::LeagueRow> Standings(const std::vector<const Row*>& rows) const;

    std::string mPath;
    std::vector<Row> mRows;                                  // everything scored, oldest first
    std::map<std::string, int> mDuelsPerDay;                 // "YYYY-MM-DD|deviceA|deviceB" -> scored duels
    std::size_t mRaces = 0;
};
}

#endif

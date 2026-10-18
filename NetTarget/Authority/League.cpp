#include "League.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>
#include <sstream>

namespace Authority
{
namespace
{
constexpr int kDuelsPerDay = 3;
constexpr int kWinPoints = 3;

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::tm Utc(std::int64_t t)
{
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &tt);
#else
    gmtime_r(&tt, &parts);
#endif
    return parts;
}

std::string Day(std::int64_t t)
{
    const std::tm p = Utc(t);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", p.tm_year + 1900, p.tm_mon + 1, p.tm_mday);
    return text;
}

int YearOf(std::int64_t t) { return Utc(t).tm_year + 1900; }

std::string CleanName(const std::string& name)   // a name in the log is one tab-free line
{
    std::string out;
    for (char c : name) out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    return out;
}
}

std::string League::MonthOf(std::int64_t unixTime)
{
    const std::tm p = Utc(unixTime);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d", p.tm_year + 1900, p.tm_mon + 1);
    return text;
}

std::string League::DeviceKey(std::uint64_t high, std::uint64_t low)
{
    char text[40];
    std::snprintf(text, sizeof text, "%016llx%016llx", static_cast<unsigned long long>(high), static_cast<unsigned long long>(low));
    return text;
}

League::League(const std::string& path) : mPath(path)
{
    if (path.empty()) return;
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return;
    char line[512];
    while (std::fgets(line, sizeof line, f) != nullptr) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        std::vector<std::string> fields;
        std::string field;
        std::istringstream in(text);
        while (std::getline(in, field, '\t')) fields.push_back(field);
        if (text.size() && text.back() == '\t') fields.push_back(std::string());
        if (fields.size() == 4 && fields[0] == "R") {           // R <time> <devices joined by ,> <device count>
            const std::int64_t t = std::atoll(fields[1].c_str());
            if (fields[3] == "2") ++mDuelsPerDay[Day(t) + "|" + fields[2]];
            ++mRaces;
        }
        else if (fields.size() == 6 && fields[0] == "S") {     // S <time> <device> <name> <points> <place>
            Row row;
            row.time = std::atoll(fields[1].c_str()); row.device = fields[2]; row.name = fields[3];
            row.points = std::atoi(fields[4].c_str()); row.place = std::atoi(fields[5].c_str());
            if (row.time > 0 && row.device.size() == 32 && row.points >= 0 && row.points <= 3 && row.place >= 1 && row.place <= 8) mRows.push_back(row);
        }
    }
    std::fclose(f);
}

void League::Append(const std::string& line)
{
    if (mPath.empty()) return;
    if (std::FILE* f = std::fopen(mPath.c_str(), "a")) { std::fputs(line.c_str(), f); std::fclose(f); }
}

void League::Add(const Row& row) { mRows.push_back(row); }

bool League::RaceIsValid(const std::vector<LeagueEntrant>& entrants, std::string* reason)
{
    auto fail = [&](const char* why) { if (reason != nullptr) *reason = why; return false; };
    if (entrants.size() < 2) return fail("fewer than two players");
    std::set<std::string> devices, names;
    for (const LeagueEntrant& e : entrants) {
        if (!e.hasDevice) return fail("a player did not send a device id (an older game)");
        if (!devices.insert(DeviceKey(e.deviceHigh, e.deviceLow)).second) return fail("two players on the same device");
        if (!names.insert(Lower(e.name)).second) return fail("two players with the same name");
    }
    return true;
}

LeagueDecision League::Record(std::int64_t unixTime, const std::vector<LeagueEntrant>& entrants)
{
    LeagueDecision d;
    d.points.assign(entrants.size(), 0);
    d.valid = RaceIsValid(entrants, &d.reason);
    if (!d.valid) return d;
    std::vector<std::size_t> finishers;
    for (std::size_t i = 0; i < entrants.size(); ++i) if (entrants[i].finished && entrants[i].finishMs > 0) finishers.push_back(i);
    if (finishers.size() < 2) { d.reason = "fewer than two players finished"; return d; }
    std::stable_sort(finishers.begin(), finishers.end(), [&](std::size_t a, std::size_t b) { return entrants[a].rank < entrants[b].rank; });
    std::vector<std::string> devices;
    for (const LeagueEntrant& e : entrants) devices.push_back(DeviceKey(e.deviceHigh, e.deviceLow));
    std::vector<std::string> sorted = devices;
    std::sort(sorted.begin(), sorted.end());
    std::string joined;
    for (const std::string& s : sorted) joined += (joined.empty() ? "" : ",") + s;
    if (entrants.size() == 2) {
        int& duels = mDuelsPerDay[Day(unixTime) + "|" + joined];
        if (duels >= kDuelsPerDay) { d.reason = "the same two devices have already scored three duels today"; return d; }
        ++duels;
    }
    d.scored = true;
    Append("R\t" + std::to_string(unixTime) + "\t" + joined + "\t" + std::to_string(entrants.size()) + "\n");
    ++mRaces;
    for (std::size_t place = 0; place < finishers.size(); ++place) {
        const std::size_t i = finishers[place];
        const int points = place == 0 ? kWinPoints : 0;   // only the winner scores; the others' races are still counted
        d.points[i] = points;
        Row row;
        row.time = unixTime; row.device = devices[i]; row.name = CleanName(entrants[i].name); row.points = points; row.place = static_cast<int>(place) + 1;
        Add(row);
        Append("S\t" + std::to_string(unixTime) + "\t" + row.device + "\t" + row.name + "\t" + std::to_string(points) + "\t" + std::to_string(row.place) + "\n");
    }
    return d;
}

std::vector<Protocol3::LeagueRow> League::Standings(const std::vector<const Row*>& rows) const
{
    struct Total { std::string name; int points = 0, wins = 0, races = 0; std::int64_t first = 0; };
    std::map<std::string, Total> totals;
    for (const Row* r : rows) {
        Total& t = totals[r->device];
        if (t.races == 0) t.first = r->time;
        t.name = r->name;   // the rows are oldest first: the last name used is the one shown
        t.points += r->points; t.races += 1; if (r->place == 1 && r->points > 0) t.wins += 1;
    }
    std::vector<Total> list;
    for (const auto& kv : totals) list.push_back(kv.second);
    std::stable_sort(list.begin(), list.end(), [](const Total& a, const Total& b) {
        if (a.points != b.points) return a.points > b.points;
        if (a.wins != b.wins) return a.wins > b.wins;
        if (a.first != b.first) return a.first < b.first;   // whoever got there first
        return a.name < b.name;
    });
    std::vector<Protocol3::LeagueRow> out;
    for (const Total& t : list) {
        Protocol3::LeagueRow row;
        row.name = t.name.substr(0, 32);
        row.points = static_cast<std::uint16_t>(std::min(t.points, 65535)); row.wins = static_cast<std::uint16_t>(std::min(t.wins, 65535)); row.races = static_cast<std::uint16_t>(std::min(t.races, 65535));
        out.push_back(row);
    }
    return out;
}

Protocol3::LeagueData League::Data(const std::string& month, std::int64_t now) const
{
    Protocol3::LeagueData data;
    data.currentMonth = MonthOf(now);
    data.month = month.empty() ? data.currentMonth : month;
    data.currentYear = static_cast<std::uint16_t>(YearOf(now));
    std::map<std::string, std::vector<const Row*>> byMonth;
    std::map<int, std::vector<const Row*>> byYear;
    for (const Row& r : mRows) { byMonth[MonthOf(r.time)].push_back(&r); byYear[YearOf(r.time)].push_back(&r); }
    auto shown = byMonth.find(data.month);
    if (shown != byMonth.end()) data.standings = Standings(shown->second);
    if (data.standings.size() > static_cast<std::size_t>(Protocol3::kMaxLeagueRows)) data.standings.resize(Protocol3::kMaxLeagueRows);
    // Finished months, newest first (months are "YYYY-MM": text order is date order).
    for (auto it = byMonth.rbegin(); it != byMonth.rend() && data.pastWinners.size() < static_cast<std::size_t>(Protocol3::kMaxLeagueMonths); ++it) {
        if (it->first >= data.currentMonth) continue;
        const std::vector<Protocol3::LeagueRow> table = Standings(it->second);
        Protocol3::LeagueMonthWinner w;
        w.month = it->first;
        if (!table.empty() && table[0].points > 0) { w.name = table[0].name; w.points = table[0].points; }
        data.pastWinners.push_back(w);
    }
    auto thisYear = byYear.find(YearOf(now));
    if (thisYear != byYear.end()) data.yearStandings = Standings(thisYear->second);
    if (data.yearStandings.size() > static_cast<std::size_t>(Protocol3::kMaxLeagueRows)) data.yearStandings.resize(Protocol3::kMaxLeagueRows);
    for (auto it = byYear.rbegin(); it != byYear.rend() && data.yearWinners.size() < static_cast<std::size_t>(Protocol3::kMaxLeagueYears); ++it) {
        if (it->first >= YearOf(now)) continue;
        const std::vector<Protocol3::LeagueRow> table = Standings(it->second);
        Protocol3::LeagueYearWinner w;
        w.year = static_cast<std::uint16_t>(it->first);
        if (!table.empty() && table[0].points > 0) { w.name = table[0].name; w.points = table[0].points; }
        data.yearWinners.push_back(w);
    }
    return data;
}
}

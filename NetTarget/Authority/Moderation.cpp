#include "Moderation.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "Hmac.h"
#include "Protocol3.h"
#include "../LinuxClient/Sha256.h"

namespace Authority
{
namespace
{
std::string Flat(std::string text)
{
    for (char& c : text) if (c == '\t' || c == '\n' || c == '\r' || static_cast<unsigned char>(c) < 0x20) c = ' ';
    if (text.size() > 300) text.resize(300);
    return text;
}

std::vector<std::string> Split(const std::string& line, char sep)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : line) { if (c == sep) { parts.push_back(part); part.clear(); } else part += c; }
    parts.push_back(part);
    return parts;
}

const char* kGenesis = "0000000000000000000000000000000000000000000000000000000000000000";
}

// ---- audit log ---------------------------------------------------------------------------------------------------
AuditLog::AuditLog(const std::string& path) : mPath(path), mLastHash(kGenesis), mSeq(0)
{
    if (path.empty()) return;
    std::ifstream f(path.c_str());
    for (std::string line; std::getline(f, line);) if (!line.empty()) mLines.push_back(line);
    std::string error;
    if (!mLines.empty()) {
        const std::vector<std::string> parts = Split(mLines.back(), '\t');
        if (parts.size() == 7) { mLastHash = parts[6]; mSeq = std::strtoull(parts[0].c_str(), nullptr, 10) + 1; }
    }
}

void AuditLog::Record(std::int64_t nowMs, const std::string& actor, const std::string& action, const std::string& detail)
{
    std::string body = std::to_string(mSeq) + "\t" + std::to_string(nowMs) + "\t" + Flat(actor) + "\t" + Flat(action) + "\t" + Flat(detail);
    const std::string hash = Sha256::HashBytes((mLastHash + "\t" + body).data(), mLastHash.size() + 1 + body.size());
    const std::string line = body + "\t" + mLastHash + "\t" + hash;
    mLines.push_back(line);
    mLastHash = hash;
    ++mSeq;
    if (!mPath.empty()) {
        std::ofstream f(mPath.c_str(), std::ios::app);
        f << line << "\n";
    }
}

bool AuditLog::Verify(const std::vector<std::string>& lines, std::string& error)
{
    std::string previous;
    std::uint64_t seq = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::vector<std::string> parts = Split(lines[i], '\t');
        if (parts.size() != 7) { error = "line " + std::to_string(i + 1) + " is malformed"; return false; }
        const std::string body = parts[0] + "\t" + parts[1] + "\t" + parts[2] + "\t" + parts[3] + "\t" + parts[4];
        const std::string expected = Sha256::HashBytes((parts[5] + "\t" + body).data(), parts[5].size() + 1 + body.size());
        if (expected != parts[6]) { error = "line " + std::to_string(i + 1) + " has been changed"; return false; }
        if (i == 0) {
            seq = std::strtoull(parts[0].c_str(), nullptr, 10);   // the first line anchors the chain (after a purge it continues an older one)
        }
        else {
            if (parts[5] != previous) { error = "line " + std::to_string(i + 1) + " does not follow the line before it (a line was removed, inserted or reordered)"; return false; }
            if (std::strtoull(parts[0].c_str(), nullptr, 10) != seq + 1) { error = "line " + std::to_string(i + 1) + " breaks the sequence numbers"; return false; }
            seq = seq + 1;
        }
        previous = parts[6];
    }
    return true;
}

bool AuditLog::VerifyFile(const std::string& path, std::string& error)
{
    std::ifstream f(path.c_str());
    if (!f) { error = "cannot read the audit log"; return false; }
    std::vector<std::string> lines;
    for (std::string line; std::getline(f, line);) if (!line.empty()) lines.push_back(line);
    return Verify(lines, error);
}

int AuditLog::Purge(std::int64_t nowMs, std::int64_t retentionMs)
{
    std::size_t keepFrom = 0;
    while (keepFrom < mLines.size()) {
        const std::vector<std::string> parts = Split(mLines[keepFrom], '\t');
        if (parts.size() != 7 || nowMs - std::strtoll(parts[1].c_str(), nullptr, 10) <= retentionMs) break;
        ++keepFrom;
    }
    if (keepFrom == 0) return 0;
    mLines.erase(mLines.begin(), mLines.begin() + static_cast<std::ptrdiff_t>(keepFrom));
    // The first kept line now anchors the chain; the purge itself is recorded after it.
    if (!mPath.empty()) {
        std::ofstream f(mPath.c_str(), std::ios::trunc);
        for (const std::string& l : mLines) f << l << "\n";
    }
    Record(nowMs, "system", "purge", "removed " + std::to_string(keepFrom) + " audit lines older than the retention period");
    return static_cast<int>(keepFrom);
}

// ---- moderation state --------------------------------------------------------------------------------------------
void Moderation::Mute(const std::string& actor, const std::string& name, std::int64_t minutes, std::int64_t nowMs)
{
    if (minutes < 1) minutes = 1;
    if (minutes > 60 * 24 * 30) minutes = 60 * 24 * 30;
    mMutes[name] = nowMs + minutes * 60 * 1000;
    mAudit.Record(nowMs, actor, "mute", name + " for " + std::to_string(minutes) + " minutes");
}

void Moderation::Unmute(const std::string& actor, const std::string& name, std::int64_t nowMs)
{
    mMutes.erase(name);
    mAudit.Record(nowMs, actor, "unmute", name);
}

bool Moderation::IsMuted(const std::string& name, std::int64_t nowMs) const
{
    auto it = mMutes.find(name);
    return it != mMutes.end() && it->second > nowMs;
}

void Moderation::Ban(const std::string& actor, const std::string& address, std::int64_t nowMs)
{
    mBans.insert(address);
    mAudit.Record(nowMs, actor, "ban", address);
}

void Moderation::Unban(const std::string& actor, const std::string& address, std::int64_t nowMs)
{
    mBans.erase(address);
    mAudit.Record(nowMs, actor, "unban", address);
}

void Moderation::Kick(const std::string& actor, const std::string& name, std::int64_t nowMs)
{
    mKicks.insert(name);
    mAudit.Record(nowMs, actor, "kick", name);
}

bool Moderation::TakeKick(const std::string& name)
{
    return mKicks.erase(name) != 0;
}

void Moderation::RememberChat(std::int64_t nowMs, const std::string& line)
{
    mChat.push_back(std::make_pair(nowMs, line));
    while (mChat.size() > 40) mChat.pop_front();
}

int Moderation::AddReport(std::int64_t nowMs, const std::string& reporter, const std::string& target, const std::string& reason)
{
    ReportEntry e;
    e.id = mNextReport++;
    e.timeMs = nowMs;
    e.reporter = reporter;
    e.target = target;
    e.reason = reason;
    const std::size_t from = mChat.size() > 15 ? mChat.size() - 15 : 0;
    for (std::size_t i = from; i < mChat.size(); ++i) e.context.push_back(mChat[i].second);
    mReports.push_back(e);
    mAudit.Record(nowMs, reporter, "report", "#" + std::to_string(e.id) + " against " + target + ": " + reason);
    return e.id;
}

bool Moderation::Resolve(const std::string& actor, int id, const std::string& note, std::int64_t nowMs)
{
    for (ReportEntry& r : mReports) {
        if (r.id == id && !r.resolved) {
            r.resolved = true;
            r.resolution = note;
            mAudit.Record(nowMs, actor, "resolve", "#" + std::to_string(id) + ": " + note);
            return true;
        }
    }
    return false;
}

void Moderation::Purge(std::int64_t nowMs, std::int64_t chatRetentionMs, std::int64_t reportRetentionMs)
{
    while (!mChat.empty() && nowMs - mChat.front().first > chatRetentionMs) mChat.pop_front();
    mReports.erase(std::remove_if(mReports.begin(), mReports.end(), [&](const ReportEntry& r) { return r.resolved && nowMs - r.timeMs > reportRetentionMs; }), mReports.end());
    for (auto it = mMutes.begin(); it != mMutes.end();) { if (it->second <= nowMs) it = mMutes.erase(it); else ++it; }
}

std::vector<std::string> Moderation::MuteList(std::int64_t nowMs) const
{
    std::vector<std::string> out;
    for (const auto& m : mMutes) if (m.second > nowMs) out.push_back(m.first + " (" + std::to_string((m.second - nowMs) / 60000 + 1) + " min left)");
    return out;
}

// ---- console -----------------------------------------------------------------------------------------------------
std::string AdminConsole::NextChallenge()
{
    mState ^= mState >> 12; mState ^= mState << 25; mState ^= mState >> 27;
    const std::uint64_t a = mState * 2685821657736338717ULL;
    mState ^= mState >> 12; mState ^= mState << 25; mState ^= mState >> 27;
    const std::uint64_t b = mState * 2685821657736338717ULL;
    char text[40];
    std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    return text;
}

AdminConsole::Session AdminConsole::Open(const std::string& address)
{
    Session s;
    s.challenge = NextChallenge();
    s.address = address;
    return s;
}

std::string AdminConsole::Greeting(const Session& s) const
{
    return "CHALLENGE " + s.challenge + "\n";
}

std::string AdminConsole::Handle(Session& s, const std::string& rawLine, std::int64_t nowMs)
{
    std::string line = rawLine;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    if (line.size() > 300) return "ERR line too long\n";
    std::istringstream words(line);
    std::string command;
    words >> command;
    if (command.empty()) return "";

    if (s.moderator.empty()) {
        if (command != "AUTH") return "ERR authenticate first: AUTH <name> <hmac>\n";
        std::string name, proof;
        words >> name >> proof;
        auto secret = mSecrets.find(name);
        const bool known = secret != mSecrets.end();
        const std::string expected = Hmac::Sha256Hex(known ? secret->second : std::string("x"), s.challenge + ":" + name);
        if (known && Hmac::ConstantTimeEquals(expected, proof)) {
            s.moderator = name;
            mModeration.Audit().Record(nowMs, name, "login", "from " + s.address);
            return "OK welcome " + name + "\n";
        }
        ++s.failures;
        mModeration.Audit().Record(nowMs, s.address, "login-failed", "as " + name);
        if (s.failures >= 3) { s.closed = true; return "ERR too many failures\n"; }
        return "ERR authentication failed\n";
    }

    auto rest = [&]() { std::string r; std::getline(words, r); while (!r.empty() && r.front() == ' ') r.erase(r.begin()); return r; };
    if (command == "QUIT") { s.closed = true; mModeration.Audit().Record(nowMs, s.moderator, "logout", ""); return "OK bye\n"; }
    if (command == "HELP") return "REPORTS | REPORT <id> | RESOLVE <id> <note> | MUTE <name> <minutes> | UNMUTE <name> | MUTES | KICK <name> | BAN <address> | UNBAN <address> | AUDIT-CHECK | QUIT\n";
    if (command == "REPORTS") {
        std::string out;
        for (const ReportEntry& r : mModeration.Reports()) if (!r.resolved) out += "#" + std::to_string(r.id) + " " + r.reporter + " reports " + r.target + ": " + r.reason + "\n";
        return out + "OK\n";
    }
    if (command == "REPORT") {
        int id = 0;
        words >> id;
        for (const ReportEntry& r : mModeration.Reports()) {
            if (r.id != id) continue;
            std::string out = "#" + std::to_string(r.id) + " " + r.reporter + " reports " + r.target + ": " + r.reason + (r.resolved ? " [resolved: " + r.resolution + "]" : "") + "\n";
            for (const std::string& c : r.context) out += "  chat: " + c + "\n";
            mModeration.Audit().Record(nowMs, s.moderator, "view-report", "#" + std::to_string(id));
            return out + "OK\n";
        }
        return "ERR no such report\n";
    }
    if (command == "RESOLVE") {
        int id = 0;
        words >> id;
        const std::string note = rest();
        return mModeration.Resolve(s.moderator, id, note.empty() ? "no note" : note, nowMs) ? "OK\n" : "ERR no such open report\n";
    }
    if (command == "MUTE") {
        std::string name; long long minutes = 0;
        words >> name >> minutes;
        if (name.empty() || minutes < 1) return "ERR usage: MUTE <name> <minutes>\n";
        mModeration.Mute(s.moderator, name, minutes, nowMs);
        return "OK\n";
    }
    if (command == "UNMUTE") { std::string name; words >> name; if (name.empty()) return "ERR usage: UNMUTE <name>\n"; mModeration.Unmute(s.moderator, name, nowMs); return "OK\n"; }
    if (command == "MUTES") {
        std::string out;
        for (const std::string& m : mModeration.MuteList(nowMs)) out += m + "\n";
        return out + "OK\n";
    }
    if (command == "KICK") { std::string name; words >> name; if (name.empty()) return "ERR usage: KICK <name>\n"; mModeration.Kick(s.moderator, name, nowMs); return "OK\n"; }
    if (command == "BAN") { std::string a; words >> a; if (a.empty()) return "ERR usage: BAN <address>\n"; mModeration.Ban(s.moderator, a, nowMs); return "OK\n"; }
    if (command == "UNBAN") { std::string a; words >> a; if (a.empty()) return "ERR usage: UNBAN <address>\n"; mModeration.Unban(s.moderator, a, nowMs); return "OK\n"; }
    if (command == "AUDIT-CHECK") {
        std::string error;
        return AuditLog::Verify(mModeration.Audit().Lines(), error) ? "OK the audit log is intact\n" : "ERR " + error + "\n";
    }
    return "ERR unknown command (HELP lists them)\n";
}
}

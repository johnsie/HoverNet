// Moderation.h -- the minimal moderation workflow of HoverNet's online services (Stage 6a): an audit log that cannot be edited
// unnoticed, mutes, address bans, removals, player reports with the chat that led to them, and an authenticated console for the
// moderators. Transport-free: the server hands the console lines of text and sends back what it returns.
#ifndef MODERATION_H
#define MODERATION_H

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Authority
{
// Append-only record of moderation and security events. Every line carries a sequence number, the time, who did it, what, the
// details, the hash of the previous line and its own hash (SHA-256 over the previous hash and the line), so a deleted, edited,
// reordered or inserted line breaks the chain from that point on.
class AuditLog
{
public:
    explicit AuditLog(const std::string& path = std::string());   // an empty path keeps the log in memory only
    void Record(std::int64_t nowMs, const std::string& actor, const std::string& action, const std::string& detail);
    const std::vector<std::string>& Lines() const { return mLines; }
    static bool Verify(const std::vector<std::string>& lines, std::string& error);
    static bool VerifyFile(const std::string& path, std::string& error);
    // Removes lines older than the retention period by starting a new chain whose first line records the hash it continues from.
    // (The records of what was removed are summarised, not kept.) Returns the number of lines dropped.
    int Purge(std::int64_t nowMs, std::int64_t retentionMs);

private:
    std::string mPath;
    std::vector<std::string> mLines;
    std::string mLastHash;
    std::uint64_t mSeq;
};

struct ReportEntry
{
    int id = 0;
    std::int64_t timeMs = 0;
    std::string reporter, target, reason;
    std::vector<std::string> context;     // the chat lines just before the report
    bool resolved = false;
    std::string resolution;
};

class Moderation
{
public:
    explicit Moderation(AuditLog& audit) : mAudit(audit), mNextReport(1) {}

    // ---- sanctions ----
    void Mute(const std::string& actor, const std::string& name, std::int64_t minutes, std::int64_t nowMs);
    void Unmute(const std::string& actor, const std::string& name, std::int64_t nowMs);
    bool IsMuted(const std::string& name, std::int64_t nowMs) const;
    void Ban(const std::string& actor, const std::string& address, std::int64_t nowMs);
    void Unban(const std::string& actor, const std::string& address, std::int64_t nowMs);
    bool IsBanned(const std::string& address) const { return mBans.count(address) != 0; }
    void Kick(const std::string& actor, const std::string& name, std::int64_t nowMs);
    bool TakeKick(const std::string& name);       // true once if a moderator asked for this name to be removed

    // ---- chat context and reports ----
    void RememberChat(std::int64_t nowMs, const std::string& line);
    int AddReport(std::int64_t nowMs, const std::string& reporter, const std::string& target, const std::string& reason);
    const std::vector<ReportEntry>& Reports() const { return mReports; }
    bool Resolve(const std::string& actor, int id, const std::string& note, std::int64_t nowMs);

    // ---- retention ----
    // Chat context and resolved reports older than the retention period are deleted; expired mutes are dropped.
    void Purge(std::int64_t nowMs, std::int64_t chatRetentionMs, std::int64_t reportRetentionMs);

    AuditLog& Audit() { return mAudit; }
    std::vector<std::string> MuteList(std::int64_t nowMs) const;

private:
    AuditLog& mAudit;
    std::map<std::string, std::int64_t> mMutes;      // name -> mute ends (ms)
    std::set<std::string> mBans;
    std::set<std::string> mKicks;
    std::deque<std::pair<std::int64_t, std::string>> mChat;
    std::vector<ReportEntry> mReports;
    int mNextReport;
};

// The moderators' console. A session starts with a random challenge; the moderator proves who they are with
// HMAC-SHA256(secret, challenge + ":" + name) and then types commands, one per line. Every command, and every failed attempt,
// is written to the audit log under the moderator's name (or the address for a failure).
class AdminConsole
{
public:
    // secrets: moderator name -> shared secret. challengeSeed makes the challenges unpredictable (the server seeds it from the OS).
    AdminConsole(Moderation& moderation, const std::map<std::string, std::string>& secrets, std::uint64_t challengeSeed)
        : mModeration(moderation), mSecrets(secrets), mState(challengeSeed ? challengeSeed : 1) {}

    class Session
    {
    public:
        std::string challenge;
        std::string moderator;      // empty until authenticated
        int failures = 0;
        bool closed = false;
        std::string address;
    };

    Session Open(const std::string& address);
    std::string Greeting(const Session& s) const;
    // Handles one line from the console and returns the reply (possibly several lines, each ending in a newline).
    std::string Handle(Session& s, const std::string& line, std::int64_t nowMs);

private:
    std::string NextChallenge();
    Moderation& mModeration;
    std::map<std::string, std::string> mSecrets;
    std::uint64_t mState;
};
}

#endif

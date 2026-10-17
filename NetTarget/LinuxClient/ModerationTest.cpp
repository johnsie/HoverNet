// Tests of the online-services basics (Stage 6a): the tamper-evident audit log, HMAC authentication of moderators, the moderators'
// console, and chat with mute, block and report inside an authoritative race.
//
//   HoverNetModerationTest
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/Hmac.h"
#include "../Authority/Moderation.h"
#include "../Authority/RaceHost.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

struct Player
{
    RaceHost* host = nullptr;
    int conn = 0;
    Protocol3::FrameParser parser;
    std::vector<Protocol3::ChatMessage> chat;
    bool closed() const { return host->IsClosed(conn); }
    void Send(std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now)
    {
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(type, payload);
        host->Receive(conn, f.data(), f.size(), now);
    }
    void Pump()
    {
        const std::vector<std::uint8_t> bytes = host->TakeOutput(conn);
        parser.Feed(bytes.data(), bytes.size());
        std::uint8_t type;
        std::vector<std::uint8_t> payload;
        while (parser.Next(type, payload)) {
            if (type == Protocol3::kChatMessage) { Protocol3::ChatMessage m; if (Protocol3::Decode(payload, m)) chat.push_back(m); }
        }
    }
    bool Heard(const std::string& text) const { for (const auto& m : chat) if (m.text == text) return true; return false; }
};

void Say(Player& p, const std::string& text, std::int64_t now)
{
    Protocol3::Chat c;
    c.text = text;
    p.Send(Protocol3::kChat, Protocol3::Encode(c), now);
}
}

int main()
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    // ---- HMAC-SHA256 against RFC 4231 test vectors ---------------------------------------------------------------
    Check(Hmac::Sha256Hex(std::string(20, '\x0b'), "Hi There") == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "HMAC-SHA256 test case 1");
    Check(Hmac::Sha256Hex("Jefe", "what do ya want for nothing?") == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "HMAC-SHA256 test case 2");
    Check(Hmac::Sha256Hex(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First") == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "HMAC-SHA256 test case 6 (key longer than a block)");

    // ---- the audit log -------------------------------------------------------------------------------------------
    {
        AuditLog log;
        for (int i = 0; i < 6; ++i) log.Record(1000 + i, "mod", "mute", "player" + std::to_string(i));
        std::string error;
        Check(AuditLog::Verify(log.Lines(), error), "a fresh audit log verifies");
        std::vector<std::string> edited = log.Lines();
        edited[2].replace(edited[2].find("player2"), 7, "playerX");
        Check(!AuditLog::Verify(edited, error), "an edited line is detected");
        std::vector<std::string> removed = log.Lines();
        removed.erase(removed.begin() + 3);
        Check(!AuditLog::Verify(removed, error), "a removed line is detected");
        std::vector<std::string> swapped = log.Lines();
        std::swap(swapped[1], swapped[2]);
        Check(!AuditLog::Verify(swapped, error), "reordered lines are detected");
        std::vector<std::string> inserted = log.Lines();
        inserted.insert(inserted.begin() + 2, log.Lines()[1]);
        Check(!AuditLog::Verify(inserted, error), "an inserted line is detected");

        // On disk, and continuing after a restart.
        const std::string path = "hovernet-audit-test.log";   // in the working directory (portable)
        std::remove(path.c_str());
        {
            AuditLog a(path);
            a.Record(1, "mod", "ban", "10.0.0.1");
            a.Record(2, "mod", "unban", "10.0.0.1");
        }
        {
            AuditLog b(path);
            b.Record(3, "mod", "mute", "someone");
        }
        Check(AuditLog::VerifyFile(path, error), "an audit log on disk verifies, including after a restart");
        // Retention: old lines go, the rest still verifies and the purge is itself recorded.
        AuditLog old;
        old.Record(0, "mod", "mute", "a");
        old.Record(1000, "mod", "mute", "b");
        old.Record(10000000, "mod", "mute", "c");
        const int dropped = old.Purge(10000000, 5000000);
        Check(dropped == 2, "lines older than the retention period are purged");
        Check(AuditLog::Verify(old.Lines(), error) && old.Lines().size() == 2, "the purged log still verifies and records the purge");
        std::remove(path.c_str());
    }

    // ---- the moderators' console ---------------------------------------------------------------------------------
    {
        AuditLog audit;
        Moderation moderation(audit);
        std::map<std::string, std::string> secrets;
        secrets["ada"] = "correct horse battery staple";
        AdminConsole console(moderation, secrets, 7);
        AdminConsole::Session s = console.Open("127.0.0.1");
        Check(Contains(console.Greeting(s), "CHALLENGE "), "a session starts with a challenge");
        Check(Contains(console.Handle(s, "MUTE bob 5", 1000), "authenticate first"), "commands need authentication first");
        Check(Contains(console.Handle(s, "AUTH ada deadbeef", 1000), "failed"), "a wrong proof is refused");
        Check(Contains(console.Handle(s, "AUTH mallory " + Hmac::Sha256Hex("guess", s.challenge + ":mallory"), 1000), "failed"), "an unknown moderator is refused");
        Check(!moderation.IsMuted("bob", 1000), "nothing was muted by an unauthenticated session");
        const std::string proof = Hmac::Sha256Hex(secrets["ada"], s.challenge + ":ada");
        Check(Contains(console.Handle(s, "AUTH ada " + proof, 2000), "OK"), "the right proof is accepted");
        // A proof is for one session: another session's challenge differs.
        AdminConsole::Session other = console.Open("127.0.0.1");
        Check(other.challenge != s.challenge, "every session has its own challenge");
        Check(Contains(console.Handle(other, "AUTH ada " + proof, 2000), "failed"), "a proof from another session does not work");

        Check(Contains(console.Handle(s, "MUTE bob 5", 3000), "OK"), "a moderator can mute");
        Check(moderation.IsMuted("bob", 3000 + 299999) && !moderation.IsMuted("bob", 3000 + 300001), "the mute ends when its time is up");
        console.Handle(s, "BAN 10.9.9.9", 3100);
        Check(moderation.IsBanned("10.9.9.9"), "a moderator can ban an address");
        console.Handle(s, "UNBAN 10.9.9.9", 3200);
        Check(!moderation.IsBanned("10.9.9.9"), "and lift it");
        Check(Contains(console.Handle(s, "NONSENSE", 3300), "unknown command"), "an unknown command is answered");
        Check(Contains(console.Handle(s, "AUDIT-CHECK", 3400), "intact"), "the console can check the audit log");
        // Three failures close a session.
        AdminConsole::Session brute = console.Open("203.0.113.5");
        for (int i = 0; i < 3; ++i) console.Handle(brute, "AUTH ada 00", 4000 + i);
        Check(brute.closed, "three failed attempts close the session");
        // Everything is in the audit log, under the moderator's name or the address that failed.
        std::string joined;
        for (const std::string& l : audit.Lines()) joined += l + "\n";
        Check(Contains(joined, "\tada\tlogin\t") && Contains(joined, "\tada\tmute\tbob for 5 minutes") && Contains(joined, "login-failed") && Contains(joined, "203.0.113.5") && Contains(joined, "\tada\tban\t10.9.9.9"),
              "logins, failures and every moderation action are in the audit log");
        std::string error;
        Check(AuditLog::Verify(audit.Lines(), error), "and the audit log verifies");
    }

    // ---- chat, mute, block and report in a race --------------------------------------------------------------------
    {
        AuditLog audit;
        Moderation moderation(audit);
        HostConfig cfg;
        cfg.race.trackName = "ClassicH";
        cfg.race.trackPath = SimHarness::TrackPath("ClassicH");
        cfg.race.players = 3;
        cfg.startDelayMs = 0;
        cfg.moderation = &moderation;
        RaceHost host(cfg);
        std::string error;
        host.Init(error);
        Player p[3];
        const char* names[3] = {"ann", "ben", "cat"};
        for (int i = 0; i < 3; ++i) {
            p[i].host = &host;
            p[i].conn = host.Connect(0);
            Protocol3::Hello h;
            h.name = names[i];
            p[i].Send(Protocol3::kHello, Protocol3::Encode(h), 0);
        }
        Check(host.Started(), "the race has started");
        for (auto& x : p) x.Pump();

        Say(p[0], "hello everyone", 100);
        for (auto& x : p) x.Pump();
        Check(p[0].Heard("hello everyone") && p[1].Heard("hello everyone") && p[2].Heard("hello everyone"), "a chat message reaches every player, including the sender");

        // Block: ben blocks ann; cat does not.
        p[1].Send(Protocol3::kBlock, Protocol3::Encode(Protocol3::Block{"ann"}), 200);
        Say(p[0], "second message", 1500);
        for (auto& x : p) x.Pump();
        Check(!p[1].Heard("second message") && p[2].Heard("second message") && p[0].Heard("second message"), "a blocked name's messages are not shown to the blocker only");

        // Text rules and rate limit.
        const int invalidBefore = host.InvalidTotal();
        Protocol3::Chat bad;
        bad.text = std::string("line\nbreak");
        p[2].Send(Protocol3::kChat, Protocol3::Encode(bad), 3000);
        Check(host.InvalidTotal() > invalidBefore, "chat with a control byte is refused as invalid");
        for (int i = 0; i < 10; ++i) Say(p[2], "spam " + std::to_string(i), 5000);
        for (auto& x : p) x.Pump();
        int spamHeard = 0;
        for (int i = 0; i < 10; ++i) if (p[0].Heard("spam " + std::to_string(i))) ++spamHeard;
        Check(spamHeard <= 3, "a burst of chat is limited to the allowed burst");
        bool notified = false;
        for (const auto& m : p[2].chat) if (m.fromServer && m.text.find("Slow down") != std::string::npos) notified = true;
        Check(notified, "the sender is told to slow down");

        // Report: carries the recent chat; rate limited; cannot report yourself.
        p[1].Send(Protocol3::kReport, Protocol3::Encode(Protocol3::Report{"ann", "abusive language"}), 20000);
        Check(moderation.Reports().size() == 1 && moderation.Reports()[0].target == "ann" && moderation.Reports()[0].reporter == "ben", "a report reaches the moderators");
        Check(!moderation.Reports().empty() && !moderation.Reports()[0].context.empty() && Contains(moderation.Reports()[0].context[0], "hello everyone"), "with the chat that led to it");
        p[1].Send(Protocol3::kReport, Protocol3::Encode(Protocol3::Report{"ben", "myself"}), 20100);
        Check(moderation.Reports().size() == 1, "a player cannot report themselves");
        for (int i = 0; i < 5; ++i) p[1].Send(Protocol3::kReport, Protocol3::Encode(Protocol3::Report{"cat", "flood " + std::to_string(i)}), 20200 + i);
        Check(moderation.Reports().size() <= 4, "reports are rate limited");

        // Mute by a moderator: ann's chat is no longer delivered and she is told.
        moderation.Mute("ada", "ann", 10, 30000);
        Say(p[0], "can anyone hear me", 40000);
        for (auto& x : p) x.Pump();
        Check(!p[1].Heard("can anyone hear me") && !p[2].Heard("can anyone hear me"), "a muted player's chat is not delivered");
        bool told = false;
        for (const auto& m : p[0].chat) if (m.fromServer && m.text == "You are muted.") told = true;
        Check(told, "and the player is told they are muted");
        Check(!p[0].closed(), "a mute does not remove the player from the race");

        // Kick by a moderator removes the connection.
        moderation.Kick("ada", "cat", 50000);
        host.Advance(50016);
        Check(p[2].closed(), "a moderator's kick removes the player");
        std::string joined;
        for (const std::string& l : moderation.Audit().Lines()) joined += l + "\n";
        Check(Contains(joined, "\tben\treport\t") && Contains(joined, "\tada\tkick\tcat"), "reports and kicks are in the audit log");
        std::string verifyError;
        Check(AuditLog::Verify(moderation.Audit().Lines(), verifyError), "the audit log verifies after all of it");

        // Retention: the chat context goes with the retention period.
        moderation.Purge(30000 + 100000000, 1000, 1000000000);
    }

    std::printf("moderation tests %s\n", gFailures == 0 ? "passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}

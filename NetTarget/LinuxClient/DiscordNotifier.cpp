#include "DiscordNotifier.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace
{
constexpr std::size_t kMaxQueuedMessages = 100;

std::string PlayerList(const Authority::RaceHost& race)
{
    std::string out;
    for (int slot = 0; slot < race.PlayerSlots(); ++slot) {
        const std::string& name = race.SlotName(slot);
        if (name.empty()) continue;
        if (!out.empty()) out += ", ";
        out += DiscordNotifier::EscapeDiscord(name);
    }
    return out;
}
}

DiscordNotifier::DiscordNotifier(const std::string& webhookUrl) : mWebhookUrl(webhookUrl)
{
    if (Enabled()) mWorker = std::thread(&DiscordNotifier::Run, this);
}

DiscordNotifier::~DiscordNotifier()
{
    if (!mWorker.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopping = true;
    }
    mWake.notify_one();
    mWorker.join();
}

std::string DiscordNotifier::EscapeDiscord(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        // A zero-width space after @ prevents player names from creating Discord mentions.
        if (c == '@') out += "@\xE2\x80\x8B";
        else {
            if (c == '\\' || c == '*' || c == '_' || c == '`' || c == '~' || c == '|') out += '\\';
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string DiscordNotifier::JsonBody(const std::string& content)
{
    std::string out = "{\"content\":\"";
    for (unsigned char c : content) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            }
            else out += static_cast<char>(c);
        }
    }
    // Disable all Discord mention parsing as a second line of defence in case a
    // future message includes user-controlled text that was not escaped above.
    return out + "\",\"allowed_mentions\":{\"parse\":[]}}";
}

std::string DiscordNotifier::FormatDuration(std::int64_t milliseconds)
{
    if (milliseconds < 0) return "did not finish";
    const std::int64_t minutes = milliseconds / 60000;
    const std::int64_t seconds = (milliseconds / 1000) % 60;
    const std::int64_t millis = milliseconds % 1000;
    std::ostringstream out;
    out << minutes << ':' << std::setfill('0') << std::setw(2) << seconds
        << '.' << std::setw(3) << millis;
    return out.str();
}

void DiscordNotifier::LobbyJoined(const std::string& name)
{
    Post("✅ **" + EscapeDiscord(name) + "** joined the HoverNet lobby.");
}

void DiscordNotifier::RaceStarted(std::uint32_t id, const Authority::RaceHost& race)
{
    const Authority::RaceConfig& cfg = race.Config().race;
    const std::string host = race.SlotName(0).empty() ? "A player" : "**" + EscapeDiscord(race.SlotName(0)) + "**";
    std::ostringstream out;
    out << "🚦 " << host << " started race #" << id << ": **" << EscapeDiscord(cfg.trackName)
        << "** — " << cfg.laps << " lap" << (cfg.laps == 1 ? "" : "s")
        << ", " << race.PlayerSlots() << " racer" << (race.PlayerSlots() == 1 ? "" : "s")
        << ", weapons " << (cfg.weapons ? "on" : "off") << '.';
    const std::string players = PlayerList(race);
    if (!players.empty()) out << "\nRacers: " << players;
    Post(out.str());
}

void DiscordNotifier::RaceFinished(std::uint32_t id, const Authority::RaceHost& race)
{
    const Authority::RaceConfig& cfg = race.Config().race;
    std::vector<Authority::PlayerResult> results = race.Race().Results();
    std::stable_sort(results.begin(), results.end(), [](const Authority::PlayerResult& a, const Authority::PlayerResult& b) {
        return a.rank < b.rank;
    });
    std::ostringstream out;
    out << "🏁 Race #" << id << " finished: **" << EscapeDiscord(cfg.trackName) << "** — "
        << cfg.laps << " lap" << (cfg.laps == 1 ? "" : "s") << ".";
    for (const Authority::PlayerResult& result : results) {
        out << '\n' << result.rank << ". " << EscapeDiscord(race.SlotName(result.player)) << " — ";
        if (result.finished) out << FormatDuration(result.finishTimeMs) << " (best lap " << FormatDuration(result.bestLapMs) << ')';
        else out << result.laps << '/' << cfg.laps << " laps";
    }
    Post(out.str());
}

void DiscordNotifier::Post(const std::string& content)
{
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.size() >= kMaxQueuedMessages) {
        std::fprintf(stderr, "discord: notification queue full; dropping message\n");
        return;
    }
    mQueue.push_back(JsonBody(content.substr(0, 1900)));
    mWake.notify_one();
}

void DiscordNotifier::Run()
{
    for (;;) {
        std::string body;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mWake.wait(lock, [&]() { return mStopping || !mQueue.empty(); });
            if (mQueue.empty() && mStopping) return;
            body = std::move(mQueue.front());
            mQueue.pop_front();
        }
        if (!Send(body)) std::fprintf(stderr, "discord: webhook delivery failed\n");
    }
}

bool DiscordNotifier::Send(const std::string& body) const
{
    const char* args[] = {
#ifdef _WIN32
        "curl.exe",
#else
        "curl",
#endif
        "--fail", "--silent", "--show-error", "--retry", "2", "--connect-timeout", "5", "--max-time", "10",
        "--header", "Content-Type: application/json", "--data-binary", body.c_str(), mWebhookUrl.c_str(), nullptr
    };
#ifdef _WIN32
    return _spawnvp(_P_WAIT, args[0], args) == 0;
#else
    pid_t child = 0;
    if (posix_spawnp(&child, args[0], nullptr, nullptr, const_cast<char* const*>(args), environ) != 0) return false;
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

// DiscordNotifier.h -- asynchronous Discord webhook announcements for the authoritative server.
#ifndef HOVERNET_DISCORD_NOTIFIER_H
#define HOVERNET_DISCORD_NOTIFIER_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../Authority/RaceHost.h"

class DiscordNotifier
{
public:
    explicit DiscordNotifier(const std::string& webhookUrl);
    ~DiscordNotifier();

    DiscordNotifier(const DiscordNotifier&) = delete;
    DiscordNotifier& operator=(const DiscordNotifier&) = delete;

    bool Enabled() const { return !mWebhookUrl.empty(); }
    void LobbyJoined(const std::string& name);
    void RaceStarted(std::uint32_t id, const Authority::RaceHost& race);
    void RaceFinished(std::uint32_t id, const Authority::RaceHost& race);

    // Public for the small formatting test; Post() is the only network boundary.
    static std::string EscapeDiscord(const std::string& text);
    static std::string JsonBody(const std::string& content);
    static std::string FormatDuration(std::int64_t milliseconds);

private:
    void Post(const std::string& content);
    void Run();
    bool Send(const std::string& body) const;

    std::string mWebhookUrl;
    std::mutex mMutex;
    std::condition_variable mWake;
    std::deque<std::string> mQueue;
    bool mStopping = false;
    std::thread mWorker;
};

#endif

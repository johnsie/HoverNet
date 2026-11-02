#include <cstdio>
#include <string>

#include "DiscordNotifier.h"

namespace
{
int failures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
}

int main(int argc, char** argv)
{
    if (argc == 2) {
        DiscordNotifier notifier(argv[1]);
        notifier.LobbyJoined("Test @everyone");
        return 0; // The destructor waits for the queued delivery.
    }
    Check(DiscordNotifier::EscapeDiscord("@everyone_*`") == std::string("@\xE2\x80\x8B") + "everyone\\_\\*\\`",
          "Discord markdown and mentions are escaped");
    Check(DiscordNotifier::JsonBody("a\n\"b\\c") ==
              "{\"content\":\"a\\n\\\"b\\\\c\",\"allowed_mentions\":{\"parse\":[]}}",
          "webhook content is JSON escaped and mentions are disabled");
    Check(DiscordNotifier::FormatDuration(0) == "0:00.000", "zero duration is formatted");
    Check(DiscordNotifier::FormatDuration(83456) == "1:23.456", "race duration is formatted");
    Check(DiscordNotifier::FormatDuration(-1) == "did not finish", "missing duration is formatted");
    if (failures == 0) std::printf("Discord notifier tests passed\n");
    return failures == 0 ? 0 : 1;
}

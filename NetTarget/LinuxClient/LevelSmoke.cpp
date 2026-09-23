#include "../Model/Level.h"

#include <cstdio>

int main()
{
    MR_Level level(FALSE);
    if (level.GetRoomCount() != 0 || level.GetPlayerCount() != 0) {
        std::fprintf(stderr, "New level was not initialized empty\n");
        return 1;
    }

    std::puts("Headless Level smoke test passed");
    return 0;
}
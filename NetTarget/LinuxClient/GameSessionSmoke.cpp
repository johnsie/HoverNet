#include "../Model/GameSession.h"

#include <cstdio>

int main()
{
    MR_GameSession session(FALSE);
    if (session.GetSimulationTime() != -3000 || session.GetCurrentLevel() != nullptr ||
        session.GetCurrentMazeFile() != nullptr) {
        std::fprintf(stderr, "New game session was not initialized correctly\n");
        return 1;
    }

    std::puts("GameSession smoke test passed");
    return 0;
}
// Fixed-step, input-scripted simulation run that prints a hash of the game state after every frame.
//
//   HoverNetDeterminismRun <track-name> <frames> [--trace]
//
// This is the first piece of the 3.0 determinism experiment (docs/roadmap-3.0.md, Stage 0). The
// session is advanced with MR_GameSession::SimulateStep (no wall clock), the "player" follows a fixed
// input script, and the state is hashed each frame, so two runs -- on the same machine or on different
// platforms and builds -- can be compared line for line. The output is
//
//   HoverNetDeterminismRun --tables 0                      prints the trigonometry tables
//   HoverNetDeterminismRun <track-name> <frames> --state   prints the raw state instead of hashes
//
//   <track> <frames> <chain-hash>          (the whole run folded into one 64-bit hash)
//
// and, with --trace, one "<frame> <hash>" line per frame before it, so the first frame where two runs
// differ can be found with a plain diff.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The run reads the craft's raw speeds, which are private; this is a test tool, not shipping code.
#define protected public
#define private public
#include "../MainCharacter/MainCharacter.h"
#undef private
#undef protected
#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"

#ifdef __EMSCRIPTEN__
void HoverNetInitializeWebFactory();
#endif

namespace
{
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void Mix(std::uint64_t& hash, std::uint64_t value)
{
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xFF;
        hash *= kFnvPrime;
    }
}

// Exact bit pattern: any one-bit difference in any floating-point state is a divergence.
void MixDouble(std::uint64_t& hash, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Mix(hash, bits);
}

// The player's inputs for a frame: a pure function of the frame number, so every run sees the same.
int ControlsForFrame(int frame, int craft)
{
    if (craft == 1) {
        // The second craft drives a different line, fires often and jumps at other times.
        int controls = MR_MainCharacter::eMotorOn;
        const int phase = (frame / 70) % 3;
        if (phase == 0) controls |= MR_MainCharacter::eRight;
        else if (phase == 1) controls |= MR_MainCharacter::eLeft;
        if (frame % 90 == 40) controls |= MR_MainCharacter::eFire;
        if (frame % 310 >= 100 && frame % 310 < 112) controls |= MR_MainCharacter::eJump;
        if (frame % 380 == 300) controls |= MR_MainCharacter::eSelectWeapon;
        return controls;
    }

    int controls = MR_MainCharacter::eMotorOn;
    const int phase = (frame / 45) % 4;
    if (phase == 0) controls |= MR_MainCharacter::eLeft;
    else if (phase == 2) controls |= MR_MainCharacter::eRight;
    if (frame % 240 >= 200 && frame % 240 < 215) controls |= MR_MainCharacter::eJump;
    if (frame % 60 == 30) controls |= MR_MainCharacter::eFire;
    if (frame % 410 == 350) controls |= MR_MainCharacter::eSelectWeapon;
    if (frame % 500 >= 400 && frame % 500 < 430) controls |= MR_MainCharacter::eBreakDirection;
    return controls;
}
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <track-name> <frames> [--trace]\n", argv[0]);
        return 2;
    }
    const std::string trackName = argv[1];
    const int frames = std::atoi(argv[2]);
    const bool trace = argc > 3 && (std::strcmp(argv[3], "--trace") == 0 || std::strcmp(argv[3], "--state") == 0);
    const bool state = argc > 3 && std::strcmp(argv[3], "--state") == 0;
    constexpr int kFrameMs = 16;

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
#ifdef __EMSCRIPTEN__
    HoverNetInitializeWebFactory();
#endif
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    if (trackName == "--tables") {
        // The trigonometry tables are built with the platform's libm; list them to compare platforms.
        for (int angle = 0; angle < MR_2PI; ++angle) {
            std::printf("T %d %d %d\n", angle, static_cast<int>(MR_Cos[angle]), static_cast<int>(MR_Sin[angle]));
        }
        return 0;
    }

    MR_RecordFile* track = new MR_RecordFile;
    const std::string path = "NetTarget/Tracks/" + trackName + ".trk";
    if (!track->OpenForRead(path.c_str())) {
        std::fprintf(stderr, "Could not open %s\n", path.c_str());
        delete track;
        return 1;
    }
    MR_GameSession session(FALSE);
    if (!session.LoadNew(trackName.c_str(), track) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load %s\n", path.c_str());
        return 1;
    }

    MR_Level* level = session.GetCurrentLevel();
    const int startRoom = level->GetStartingRoom(0);
    MR_MainCharacter* player = MR_MainCharacter::New(5, TRUE);
    if (player == nullptr || startRoom < 0 || startRoom >= level->GetRoomCount()) {
        std::fprintf(stderr, "Could not create a player\n");
        return 1;
    }
    player->mPosition = level->GetStartingPos(0);
    player->SetOrientation(level->GetStartingOrientation(0));
    player->mRoom = startRoom;
    if (level->InsertElement(player, startRoom, FALSE) == nullptr) {
        std::fprintf(stderr, "Could not insert the player\n");
        return 1;
    }
    // A second craft on the next grid slot, so collisions between craft and missile hits are covered too.
    MR_MainCharacter* rival = MR_MainCharacter::New(5, TRUE);
    const int rivalRoom = level->GetStartingRoom(1);
    if (rival != nullptr && rivalRoom >= 0 && rivalRoom < level->GetRoomCount()) {
        rival->mPosition = level->GetStartingPos(1);
        rival->SetOrientation(level->GetStartingOrientation(1));
        rival->mRoom = rivalRoom;
        if (level->InsertElement(rival, rivalRoom, FALSE) == nullptr) rival = nullptr;
    }
    else {
        rival = nullptr;
    }
    session.SetSimulationTime(0);

    std::uint64_t chain = kFnvOffset;
    int maxElements = 0;
    for (int frame = 0; frame < frames; ++frame) {
        player->SetControlState(ControlsForFrame(frame, 0), session.GetSimulationTime());
        if (rival != nullptr) rival->SetControlState(ControlsForFrame(frame, 1), session.GetSimulationTime());
        session.SimulateStep(kFrameMs);

        std::uint64_t hash = kFnvOffset;
        Mix(hash, static_cast<std::uint64_t>(session.GetSimulationTime()));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->mPosition.mX)));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->mPosition.mY)));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->mPosition.mZ)));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->mRoom)));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->mOrientation)));
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->GetCabinOrientation())));
        MixDouble(hash, player->mXSpeed);
        MixDouble(hash, player->mYSpeed);
        MixDouble(hash, player->mZSpeed);
        MixDouble(hash, player->GetFuelLevel());
        Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(player->GetTotalTime())));
        if (rival != nullptr) {
            Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(rival->mPosition.mX)));
            Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(rival->mPosition.mY)));
            Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(rival->mPosition.mZ)));
            Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(rival->mRoom)));
            Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(rival->mOrientation)));
            MixDouble(hash, rival->mXSpeed);
            MixDouble(hash, rival->mYSpeed);
            MixDouble(hash, rival->mZSpeed);
            MixDouble(hash, rival->GetFuelLevel());
        }
        // Every free element in the level (craft, missiles, mines, power-ups) in list order.
        int elementCount = 0;
        for (int room = -1; room < level->GetRoomCount(); ++room) {
            for (MR_FreeElementHandle handle = level->GetFirstFreeElement(room); handle != nullptr;
                 handle = MR_Level::GetNextFreeElement(handle)) {
                const MR_FreeElement* element = MR_Level::GetFreeElement(handle);
                Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(element->mPosition.mX)));
                Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(element->mPosition.mY)));
                Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(element->mPosition.mZ)));
                Mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(element->mOrientation)));
                ++elementCount;
            }
        }
        Mix(hash, static_cast<std::uint64_t>(elementCount));
        maxElements = elementCount > maxElements ? elementCount : maxElements;
        if (state) {
            std::printf("%d pos=%d,%d,%d room=%d or=%d cab=%d speed=%.17g,%.17g,%.17g fuel=%.17g\n", frame,
                        static_cast<int>(player->mPosition.mX), static_cast<int>(player->mPosition.mY),
                        static_cast<int>(player->mPosition.mZ), player->mRoom, static_cast<int>(player->mOrientation),
                        static_cast<int>(player->GetCabinOrientation()), player->mXSpeed, player->mYSpeed,
                        player->mZSpeed, player->GetFuelLevel());
        }
        else if (trace) std::printf("%d %016llx\n", frame, static_cast<unsigned long long>(hash));
        Mix(chain, hash);
    }
    if (trace) std::printf("# most free elements at once: %d\n", maxElements);
    std::printf("%s %d %016llx\n", trackName.c_str(), frames, static_cast<unsigned long long>(chain));
    return 0;
}

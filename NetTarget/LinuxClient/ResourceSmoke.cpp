#include "../ObjFacTools/ResourceLib.h"
#include "../ObjFacTools/ResSound.h"
#include "../ObjFac1/ObjFac1Res.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/SoundServer.h"

#include <cstdio>

int main()
{
    MR_RecordFile recordFile;
    const char* resourcePath = "NetTarget/ObjFac1.dat";
    if (!recordFile.OpenForRead(resourcePath)) {
        std::fprintf(stderr, "Unable to read ObjFac1 record table\n");
        return 1;
    }
    recordFile.SelectRecord(0);
    int magicNumber = 0;
    const UINT bytesRead = recordFile.Read(&magicNumber, sizeof(magicNumber));
    std::printf("ObjFac1 table: records=%d first-record-bytes=%lu magic=%d bytes-read=%u\n",
                recordFile.GetNbRecords(), static_cast<unsigned long>(recordFile.GetLength()), magicNumber, bytesRead);
    recordFile.Close();

    MR_ResourceLib resources(resourcePath);
    std::printf("ObjFac1 resources: bitmaps=%d actors=%d sprites=%d short-sounds=%d continuous-sounds=%d\n",
                resources.GetBitmapCount(), resources.GetActorCount(), resources.GetSpriteCount(),
                resources.GetShortSoundCount(), resources.GetContinuousSoundCount());
    if (resources.GetBitmapCount() == 0 || resources.GetActorCount() == 0 ||
        resources.GetBitmap(-1) != nullptr || resources.GetActor(-1) != nullptr) {
        std::fprintf(stderr, "ObjFac1 resource file did not expose valid resource collections\n");
        return 1;
    }
    const MR_ResShortSound* fireSound = resources.GetShortSound(MR_SND_FIRE);
    if (fireSound == nullptr || fireSound->GetSound() == nullptr) {
        std::fprintf(stderr, "ObjFac1 fire sound did not decode into a playable sound\n");
        return 1;
    }
    const MR_ResContinuousSound* motorSound = resources.GetContinuousSound(MR_SND_MOTOR);
    if (motorSound == nullptr || motorSound->GetSound() == nullptr) {
        std::fprintf(stderr, "ObjFac1 motor sound did not decode into a playable sound\n");
        return 1;
    }
    const MR_ResContinuousSound* missileMotorSound = resources.GetContinuousSound(MR_SND_MISSILE_MOTOR);
    if (missileMotorSound == nullptr || missileMotorSound->GetSound() == nullptr) {
        std::fprintf(stderr, "ObjFac1 missile motor sound did not decode into a playable sound\n");
        return 1;
    }
    if (MR_SoundServer::GetNbCopy(static_cast<MR_ShortSound*>(nullptr)) != 1 ||
        MR_SoundServer::GetNbCopy(static_cast<MR_ContinuousSound*>(nullptr)) != 1) {
        std::fprintf(stderr, "Sound server did not preserve fallback copy counts\n");
        return 1;
    }

    std::puts("ObjFac1 resource smoke test passed");
    return 0;
}
#include "../Platform/MfcCompat.h"
#include "../Util/DllObjectFactory.h"

extern "C"
{
void MR_InitModule(HMODULE module);
void MR_CleanModule();
MR_ObjectFromFactory* MR_GetObject(MR_UInt16 classId);
}

void HoverNetInitializeWebFactory()
{
    MR_InitModule(nullptr);
    MR_DllObjectFactory::RegisterLocalDll(1, MR_GetObject);
}

void HoverNetCleanWebFactory()
{
    MR_CleanModule();
}

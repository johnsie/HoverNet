#include "../ResourceCompiler/ResActorBuilder.h"
#include "../ObjFacTools/ResourceLib.h"
#include <cstdio>

namespace
{
class SolidBitmap : public MR_ResBitmap
{
public:
    SolidBitmap(int resourceId, MR_UInt8 color) : MR_ResBitmap(resourceId)
    {
        mWidth = 1000;
        mHeight = 1000;
        mXRes = 2;
        mYRes = 2;
        mSubBitmapCount = 1;
        mPlainColor = color;
        mSubBitmapList = new SubBitmap[1];
        SubBitmap& bitmap = mSubBitmapList[0];
        bitmap.mXRes = 2;
        bitmap.mYRes = 2;
        bitmap.mXResShiftFactor = 0;
        bitmap.mYResShiftFactor = 0;
        bitmap.mHaveTransparent = FALSE;
        bitmap.mBuffer = new MR_UInt8[4];
        bitmap.mColumnPtr = new MR_UInt8*[2];
        for (int index = 0; index < 4; ++index) bitmap.mBuffer[index] = color;
        bitmap.mColumnPtr[0] = bitmap.mBuffer;
        bitmap.mColumnPtr[1] = bitmap.mBuffer + 2;
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "Usage: %s <input.dat> <Manta.msh> <output.dat>\n", argv[0]);
        return 2;
    }
    try {
        MR_ResourceLib resources(argv[1]);
        // Exact HoverRace palette indices: charcoal grey=19, car red=43,
        // orange=71, and white=10.
        resources.ReplaceBitmap(new SolidBitmap(112, 19));
        resources.ReplaceBitmap(new SolidBitmap(113, 43));
        resources.ReplaceBitmap(new SolidBitmap(114, 71));
        resources.ReplaceBitmap(new SolidBitmap(115, 10));
        MR_ResActorBuilder* manta = new MR_ResActorBuilder(24);
        if (!manta->BuildFromFile(argv[2], &resources)) {
            delete manta;
            return 1;
        }
        resources.ReplaceActor(manta);
        if (!resources.Export(argv[3])) return 1;
        std::printf("Added Manta actor 24 to %s\n", argv[3]);
        return 0;
    }
    catch (...) {
        std::fprintf(stderr, "Could not augment resource archive\n");
        return 1;
    }
}

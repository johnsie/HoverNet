#include "../VideoServices/Sprite.h"
#include "../VideoServices/2DViewport.h"

#include <cstring>

MR_Sprite::MR_Sprite() : mNbItem(0), mItemHeight(0), mTotalHeight(0), mWidth(0), mData(nullptr) {}

MR_Sprite::~MR_Sprite()
{
    delete []mData;
}

int MR_Sprite::GetNbItem() const { return mNbItem; }
int MR_Sprite::GetItemHeight() const { return mItemHeight; }
int MR_Sprite::GetItemWidth() const { return mWidth; }

void MR_Sprite::Blt(int x, int y, MR_2DViewPort* destination, eAlignment horizontalAlignment,
                    eAlignment verticalAlignment, int item, int scaling) const
{
    if (destination == nullptr || mData == nullptr || item < 0 || item >= mNbItem || scaling <= 0) {
        return;
    }

    const int scaledWidth = mWidth / scaling;
    const int scaledHeight = mItemHeight / scaling;
    if (horizontalAlignment == eRight) x -= scaledWidth;
    else if (horizontalAlignment == eCenter) x -= scaledWidth / 2;
    if (verticalAlignment == eBottom) y -= scaledHeight;
    else if (verticalAlignment == eCenter) y -= scaledHeight / 2;

    const int startX = x < 0 ? -x : 0;
    const int startY = y < 0 ? -y : 0;
    const int endX = std::min(scaledWidth, destination->GetXRes() - x);
    const int endY = std::min(scaledHeight, destination->GetYRes() - y);
    if (startX >= endX || startY >= endY) {
        return;
    }

    MR_UInt8* destinationBuffer = destination->GetBuffer();
    const int destinationStride = destination->GetLineLen();
    const MR_UInt8* source = mData + item * mItemHeight * mWidth;
    for (int destinationY = startY; destinationY < endY; ++destinationY) {
        for (int destinationX = startX; destinationX < endX; ++destinationX) {
            const MR_UInt8 pixel = source[(destinationY * scaling) * mWidth + destinationX * scaling];
            if (pixel != 0) {
                destinationBuffer[(y + destinationY) * destinationStride + x + destinationX] = pixel;
            }
        }
    }
}

void MR_Sprite::StrBlt(int x, int y, const char* text, MR_2DViewPort* destination,
                       eAlignment horizontalAlignment, eAlignment verticalAlignment, int scaling) const
{
    if (text == nullptr || text[0] == 0 || scaling < 0) {
        return;
    }

    const int textLength = static_cast<int>(std::strlen(text));
    if (scaling == 0) {
        scaling = 1;
        const int naturalWidth = textLength * mWidth * 3 / 4;
        if (destination != nullptr && naturalWidth > destination->GetXRes()) {
            scaling += naturalWidth / destination->GetXRes();
        }
    }
    const int step = mWidth * 3 / (4 * scaling);
    if (horizontalAlignment == eRight) x -= textLength * step;
    else if (horizontalAlignment == eCenter) x -= textLength * step / 2;
    if (verticalAlignment == eBottom) y -= mItemHeight / scaling;
    else if (verticalAlignment == eCenter) y -= mItemHeight / (2 * scaling);

    while (*text != 0) {
        Blt(x, y, destination, eLeft, eTop, *text, scaling);
        x += step;
        ++text;
    }
}

void MR_Sprite::Serialize(CArchive& archive)
{
    if (archive.IsStoring()) {
        archive << mNbItem << mItemHeight << mTotalHeight << mWidth;
        archive.Write(mData, mTotalHeight * mWidth);
        return;
    }

    delete []mData;
    mData = nullptr;
    archive >> mNbItem >> mItemHeight >> mTotalHeight >> mWidth;
    const int dataLength = mTotalHeight * mWidth;
    if (dataLength > 0) {
        mData = new MR_UInt8[dataLength];
        archive.Read(mData, dataLength);
    }
}

const char* Ascii2Simple(const char* source)
{
    static char buffer[256];
    int index = 0;
    while (source != nullptr && source[index] != 0 && index < static_cast<int>(sizeof(buffer)) - 1) {
        const char character = source[index];
        buffer[index] = character >= 32 && character < 127 ? character - 31 : '_' - 31;
        ++index;
    }
    buffer[index] = 0;
    return buffer;
}

char Ascii2Simple(char source)
{
    return source >= 32 && source < 127 ? source - 31 : 0;
}
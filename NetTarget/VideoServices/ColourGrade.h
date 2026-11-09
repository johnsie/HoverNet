// ColourGrade.h -- the colour grade of the Modern look, as a function of one colour (so it can be applied to a palette and to true-colour images alike).
//
// The grade: brightness (a gamma on the middle tones), a very gentle S-curve for contrast, a saturation a little under the palette's, and the faintest cool in
// the shadows and warmth in the highlights. HoverNetGfx::Presenter::GradePalette applies it to the 256 palette colours; true-colour textures (the AI-enhanced
// images, VideoServices/TextureEnhance.h) go through the same function, so the brightness setting and the grade treat both the same way.
#ifndef HOVERNET_COLOUR_GRADE_H
#define HOVERNET_COLOUR_GRADE_H

#include <cstdint>

namespace HoverNetGfx
{
class ColourGrade
{
public:
    explicit ColourGrade(float brightness);   // 0.7 .. 1.4 (clamped); 1 is the grade without a brightness change

    // One colour, 0..255 a channel.
    void Apply(int r, int g, int b, int& outR, int& outG, int& outB) const;

    // One colour as a word 0x00RRGGBB.
    std::uint32_t Apply(std::uint32_t word) const;

private:
    std::uint8_t mCurve[256];   // brightness and the contrast curve, per channel
};
}

#endif

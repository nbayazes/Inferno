#pragma once

#include "Fonts.h"

namespace Inferno {
    // Loads fonts from the d2 hog file as they are higher resolution
    void LoadFonts();
    enum class AlignH { Left, Center, Right };
    enum class AlignV { Top, Center, Bottom };
    void DrawGameText(string_view str, float x, float y, FontSize size, AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top);
}
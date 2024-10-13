#pragma once

#include "Fonts.h"

namespace Inferno {
    class RenderTarget;

    extern FontAtlas Atlas;

    constexpr float FONT_LINE_SPACING = 6.0f;

    enum class AlignH { Left, Center, CenterLeft, CenterRight, Right };
    enum class AlignV { Top, Center, CenterTop, CenterBottom, Bottom };
    Vector2 MeasureString(string_view str, FontSize size);

    string_view TrimStringByLength(string_view str, FontSize size, int maxLength);
    
    void LoadFonts();
}
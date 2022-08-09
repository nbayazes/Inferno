#pragma once

#include "Fonts.h"

namespace Inferno {
    class RenderTarget;

    extern FontAtlas Atlas;

    enum class AlignH { Left, Center, CenterLeft, CenterRight, Right };
    enum class AlignV { Top, Center, CenterTop, CenterBottom, Bottom };
    Vector2 MeasureString(string_view str, FontSize size);
    // Loads fonts from the d2 hog file as they are higher resolution
    void LoadFonts();
    //void DrawGameText(string_view str, Render::Canvas2D<UIShader>& canvas, const RenderTarget& target, float x, float y, FontSize size, Color color = { 1, 1, 1 }, AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top);
}
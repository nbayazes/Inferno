#pragma once

#include "Fonts.h"
#include "Graphics/Render.Canvas.h"

namespace Inferno {
    class RenderTarget;

    // Loads fonts from the d2 hog file as they are higher resolution
    void LoadFonts();
    enum class AlignH { Left, Center, Right };
    enum class AlignV { Top, Center, Bottom };
    void DrawGameText(string_view str, Render::Canvas2D& canvas, const RenderTarget& target, float x, float y, FontSize size, Color color = { 1, 1, 1 }, AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top);
    Vector2 MeasureString(string_view str, FontSize size);
}
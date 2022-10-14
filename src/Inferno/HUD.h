#pragma once

#include "Graphics/Render.h"
#include "Graphics/Render.Canvas.h"

namespace Inferno {
    void DrawHUD(float dt);
    void PrintHudMessage(string_view msg);
}
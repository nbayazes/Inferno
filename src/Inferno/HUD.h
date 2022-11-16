#pragma once

#include "Graphics/Render.h"

namespace Inferno {
    void DrawHUD(float dt);
    void PrintHudMessage(string_view msg);
    void AddPointsToHUD(int points);
}
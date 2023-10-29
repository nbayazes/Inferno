#pragma once

#include "Types.h"

namespace Inferno {
    void DrawHUD(float dt, const Color& ambient);
    void PrintHudMessage(string_view msg);
    void AddPointsToHUD(int points);
}

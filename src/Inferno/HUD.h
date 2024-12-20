#pragma once

#include "Types.h"

namespace Inferno {
    void DrawHUD(float dt, const Color& ambient);
    void PrintHudMessage(string_view msg);
    void AddPointsToHUD(int points);
    void AddKillToHUD(string_view name); // Adds a kill to the HUD tracker

    // Resets HUD after dying
    void ResetHUD();
}

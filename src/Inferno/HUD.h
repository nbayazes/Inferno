#pragma once

namespace Inferno {
    void DrawHUD(float dt, const Color& ambient);
    void PrintHudMessage(string_view msg);
    void AddPointsToHUD(int points);

    // Adds a fading weapon flash to the HUD. Workaround until true dynamic lighting is available
    void AddWeaponFlash(const Color& color);
}

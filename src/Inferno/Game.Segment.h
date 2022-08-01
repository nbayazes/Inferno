#pragma once
#include "Level.h"

namespace Inferno {
    void SubtractLight(Level& level, Tag light, Segment& seg);
    void AddLight(Level& level, Tag light, Segment& seg);
    void ToggleLight(Level& level, Tag light);
    void UpdateFlickeringLights(Level& level, float t, float dt);
}
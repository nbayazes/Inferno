#pragma once

#include "Types.h"

namespace Inferno {
    struct HitInfo {
        float Distance = FLT_MAX; // How far the hit was from the starting point
        Vector3 Point; // Where the intersection happened
        Vector3 Normal; // The normal of the intersection
        int16 Tri = -1; // What triangle was hit (for level walls) (unused?)
        float Speed = 0;
        operator bool() const { return Distance != FLT_MAX; }
    };
}

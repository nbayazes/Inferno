#pragma once
#include "Level.h"

namespace Inferno {
    void UpdatePhysics(Level& level, double t, float dt);

    namespace Debug {
        inline Vector3 ShipPosition, ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 120> ShipVelocities{};
        inline float Steps = 0, R = 0, K = 0;
        inline Vector3 ClosestPoint;
    };
}

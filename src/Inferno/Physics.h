#pragma once
#include "Level.h"

namespace Inferno {
    void UpdatePhysics(Level& level, double t, double dt);

    namespace Debug {
        inline Vector3 ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 120> ShipVelocities{};
        inline float Steps = 0, R = 0, K = 0;
    };
}

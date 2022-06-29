#pragma once
#include "Level.h"

namespace Inferno {
    void UpdatePhysics(Level& level);

    namespace Debug {
        inline Vector3 ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 40> ShipVelocities{};
        inline float Steps, R, K;
    };
}

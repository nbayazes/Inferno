#pragma once

#include "Settings.h"

namespace Inferno::Editor {
    namespace Metrics {
        inline uint64 RaysCast = 0;
        inline uint64 RayHits = 0;
        inline uint64 CacheHits = 0;

        inline int64 LightCalculationTime = 0;

        inline void Reset() {
            RaysCast = RayHits = CacheHits = 0;
            LightCalculationTime = 0;
        }
    };

    namespace Commands {
        void LightLevel(Level&, const LightSettings&);
    }
}
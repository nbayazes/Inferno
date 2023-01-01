#pragma once

#include "Settings.h"

namespace Inferno::Editor {
    namespace Metrics {
        inline int RaysCast = 0;
        inline int RayHits = 0;
        inline int SegmentsTested = 0;
        inline int CacheHits = 0;

        inline int64 LightCalculationTime = 0;

        inline void Reset() {
            RaysCast = RayHits = SegmentsTested = CacheHits = 0;
            LightCalculationTime = 0;
        }
    };

    Color GetLightColor(const SegmentSide& side, bool enableColor);

    namespace Commands {
        void LightLevel(Level&, const LightSettings&);
    }
}
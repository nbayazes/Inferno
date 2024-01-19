#pragma once

#include "Level.h"
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
    }

    inline float LightingProgress = 0;
    inline std::atomic<uint> DoneLightWork, TotalLightWork;
    inline std::atomic RequestCancelLighting = false; // User requested lighting cancellation
    inline std::atomic LightWorkerRunning = false; // Worker is running

    // Copies the lighting results to a level
    void CopyLightResults(Level& level);

    Color GetLightColor(const SegmentSide& side, bool enableColor);

    namespace Commands {
        void LightLevel(Level&, const LightSettings&);
    }
}
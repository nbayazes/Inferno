#pragma once
#include "Level.h"

namespace Inferno {
    void CreateMatcenEffect(const Level& level, SegID segId);
    void UpdateMatcens(Level& level, float dt);
    void TriggerMatcen(Level& level, SegID segId, SegID triggerSeg);
    void InitializeMatcens(Level& level);
}

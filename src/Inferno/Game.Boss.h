#pragma once
#include "Object.h"

namespace Inferno::Game {
    // Valid teleport location within a segment for a boss
    struct TeleportTarget {
        SegID Segment;
        Vector3 Position;
    };

    span<TeleportTarget> GetTeleportSegments();
    bool UpdateBoss(Inferno::Object& boss, float dt);
    void InitBoss();
    void StartBossDeath();
}

#pragma once
#include "Game.AI.h"
#include "Object.h"

namespace Inferno::Game {
    // Valid teleport location within a segment for a boss
    struct TeleportTarget {
        SegID Segment;
        Vector3 Position;
    };

    span<TeleportTarget> GetTeleportSegments();
    void BossBehaviorD1(AIRuntime& ai, Object& boss, const RobotInfo& info, float dt);
    void InitBoss();
    void StartBossDeath();
}

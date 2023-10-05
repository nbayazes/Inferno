#pragma once
#include "Game.AI.h"
#include "Level.h"
#include "Object.h"

namespace Inferno {
    void AvoidRoomEdges(Level& level, const Ray& ray, const Object& obj, Vector3& target);
    bool SetPathGoal(Level& level, Object& obj, AIRuntime& ai, SegID goalSegment, const Vector3& goalPosition);
    void PathTowardsGoal(Level& level, Object& obj, AIRuntime& ai, float);

    namespace AI {
        void SetPath(Object& obj, const List<SegID>& path, const Vector3* endPosition = nullptr);
    }
}

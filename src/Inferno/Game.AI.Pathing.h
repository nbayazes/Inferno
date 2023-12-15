#pragma once
#include "Game.AI.h"
#include "Level.h"
#include "Object.h"

namespace Inferno {
    void AvoidRoomEdges(Level& level, const Ray& ray, const Object& obj, Vector3& target);
    bool SetPathGoal(Level& level, const Object& obj, AIRuntime& ai, SegID goalSegment, const Vector3& goalPosition, float maxDistance);
    void PathTowardsGoal(Level& level, Object& obj, AIRuntime& ai, bool alwaysFaceGoal, bool stopOnceVisible);

    namespace AI {
        void SetPath(Object& obj, const List<NavPoint>& path, const Vector3* endPosition = nullptr);
    }
}

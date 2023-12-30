#pragma once
#include "Game.AI.h"
#include "Level.h"
#include "Object.h"

namespace Inferno {
    void AvoidRoomEdges(Level& level, const Ray& ray, const Object& obj, Vector3& target);
    bool SetPathGoal(Level& level, const Object& obj, AIRuntime& ai, const NavPoint& goal, float maxDistance);

    // Returns true when the robot moves along the path
    bool PathTowardsGoal(Object& robot, AIRuntime& ai, bool alwaysFaceGoal, bool stopOnceVisible);

    namespace AI {
        void SetPath(Object& obj, const List<NavPoint>& path);
    }
}

#pragma once
#include "Object.h"

namespace Inferno {
    inline bool IsBossRobot(const Object& obj) {
        static Set<int> bossIds = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };
        return obj.Type == ObjectType::Robot && bossIds.contains(obj.ID);
    }

    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }
}

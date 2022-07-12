#pragma once

#include "Level.h"
#include "Command.h"

namespace Inferno::Editor {
    bool AlignObjectToSide(Level&, ObjID, PointTag, bool center = false);
    bool MoveObjectToSegment(Level&, ObjID, SegID);
    int GetObjectCount(const Level& level, ObjectType type);

    ObjID AddObject(Level&, PointTag, ObjectType);
    void DeleteObject(Level&, ObjID);
    void InitObject(const Level&, Object&, ObjectType type);

    void UpdateSecretLevelReturnMarker();

    inline bool IsBossRobot(const Object& obj) {
        static Set<int> bossIds = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };
        return obj.Type == ObjectType::Robot && bossIds.contains(obj.ID);
    }

    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }

    // Ensures object direction vectors are normalized
    inline void NormalizeObjectVectors(Object& obj) {
        auto forward = obj.Transform.Forward();
        forward.Normalize();
        auto up = obj.Transform.Up();
        up.Normalize();
        auto right = -forward.Cross(up);
        right.Normalize();
        obj.Transform.Forward(forward);
        obj.Transform.Right(right);
        obj.Transform.Up(up);
    }

    namespace Commands {
        extern Command MoveObjectToSide, MoveObjectToSegment, MoveObjectToUserCSys;
        extern Command AddObject;
    }
}

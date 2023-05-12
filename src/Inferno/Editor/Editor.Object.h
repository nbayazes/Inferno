#pragma once

#include "Level.h"
#include "Command.h"

namespace Inferno::Editor {
    //bool AlignObjectToSide(Level&, ObjID, PointTag);
    bool MoveObjectToSegment(Level&, ObjID, SegID);
    int GetObjectCount(const Level& level, ObjectType type);
    float GetObjectRadius(const Object& obj);

    ObjID AddObject(Level&, PointTag, ObjectType);
    void DeleteObject(Level&, ObjID);
    void InitObject(const Level&, Object&, ObjectType type, int8 id = 0);

    void UpdateSecretLevelReturnMarker();
    void UpdateObjectSegment(Level& level, Object& obj);

    inline bool IsBossRobot(const Object& obj) {
        static Set<int> bossIds = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };
        return obj.Type == ObjectType::Robot && bossIds.contains(obj.ID);
    }

    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }

    // Ensures object direction vectors are normalized
    inline void NormalizeObjectVectors(Object& obj) {
        auto forward = obj.Rotation.Forward();
        forward.Normalize();
        auto up = obj.Rotation.Up();
        up.Normalize();
        auto right = -forward.Cross(up);
        right.Normalize();
        
        obj.Rotation.Forward(forward);
        obj.Rotation.Right(right);
        obj.Rotation.Up(up);
    }

    namespace Commands {
        extern Command AlignObjectToSide;
        extern Command MoveObjectToSide, MoveObjectToSegment, MoveObjectToUserCSys;
        extern Command AddObject;
    }
}

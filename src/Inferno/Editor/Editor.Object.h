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
    void InitObject(const Level&, Object&, ObjectType type, int8 id = 0, bool fullReset = true);

    void UpdateSecretLevelReturnMarker();

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

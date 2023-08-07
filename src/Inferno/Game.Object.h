#pragma once
#include "Object.h"

namespace Inferno {
    Tuple<Vector3, Vector3> GetSubmodelOffsetAndRotation(const Object& object, const Model& model, int submodel);

    // Updates the segment of an object based on position. Returns true if the segment changed.
    bool UpdateObjectSegment(Level& level, Object& obj);

    // Updates the segment the object is in based on position and activates triggers.
    void MoveObject(Level& level, ObjID objId);

    bool IsBossRobot(const Object& obj);
    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }
}

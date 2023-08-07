#pragma once
#include "Object.h"
#include "Level.h"

namespace Inferno {
    struct SubmodelRef {
        short ID = -1; // Submodel index. -1 is unset
        Vector3 Offset; // Offset relative to submodel origin
    };

    // Gets the submodel a gun belongs to
    uint8 GetGunSubmodel(const Object& obj, uint8 gun);

    // Gets the offset and rotation (euler) of a submodel in object space. Includes animations.
    Tuple<Vector3, Vector3> GetSubmodelOffsetAndRotation(const Object& object, const Model& model, int submodel);

    // Transforms a point from submodel space to object space. Includes animations.
    Vector3 GetSubmodelOffset(const Object& obj, SubmodelRef submodel);

    // Gets the gunpoint offset submodel space and submodel index. Does not include animations.
    SubmodelRef GetLocalGunpointOffset(const Object& obj, uint8 gun);

    // Gets the gunpoint offset in object space. Includes animations.
    Vector3 GetGunpointOffset(const Object& obj, uint8 gun);

    // Updates the segment of an object based on position. Returns true if the segment changed.
    bool UpdateObjectSegment(Level& level, Object& obj);

    // Updates the segment the object is in based on position and activates triggers.
    void MoveObject(Level& level, ObjID objId);

    bool IsBossRobot(const Object& obj);
    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }
}

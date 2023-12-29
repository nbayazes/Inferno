#pragma once
#include "Object.h"
#include "Level.h"

namespace Inferno {
    struct SubmodelRef {
        short ID = -1; // Submodel index. -1 is unset
        Vector3 Offset; // Offset relative to submodel origin

        bool IsNull() const { return ID == -1; }
        explicit operator bool() const { return !IsNull(); }
    };

    // Gets the submodel a gun belongs to
    uint8 GetGunSubmodel(const Object& obj, uint8 gun);

    // Gets the offset and rotation (euler) of a submodel in object space. Includes animations.
    Tuple<Vector3, Vector3> GetSubmodelOffsetAndRotation(const Object& object, const Model& model, int submodel);

    // Gets the object space transform of a submodel
    Matrix GetSubmodelTransform(const Object& object, const Model& model, int submodel);

    // Gets the object space translation matrix of a submodel. No animation.
    Matrix GetSubmodelTranslation(const Model& model, int submodel);

    // Transforms a point from submodel space to object space. Includes animations.
    Vector3 GetSubmodelOffset(const Object& obj, SubmodelRef submodel);

    // Gets the gunpoint offset submodel space and submodel index. Does not include animations.
    SubmodelRef GetGunpointSubmodelOffset(const Object& obj, uint8 gun);

    // Gets the gunpoint offset in object space. Includes animations.
    Vector3 GetGunpointOffset(const Object& obj, uint8 gun);

    // Gets the gunpoint position in world space. Includes animations.
    Vector3 GetGunpointWorldPosition(const Object& obj, uint8 gun);

    // Updates the segment of an object based on position. Returns true if the segment changed.
    bool UpdateObjectSegment(Level& level, Object& obj);

    // Links an object to a new segment. Similar to MoveObject but without triggers. Useful for teleporting / respawning.
    void RelinkObject(Level& level, Object& obj, SegID newSegment);

    // Updates the segment the object is in based on position and activates triggers.
    void MoveObject(Level& level, Object& obj);

    bool IsBossRobot(const Object& obj);
    void CreateRobot(SegID segment, const Vector3& position, int8 type, MatcenID srcMatcen = MatcenID::None);

    // Flags an object to be destroyed
    void ExplodeObject(Object& obj, float delay = 0);

    void SpawnContained(const ContainsData& contains, Level& level, const Vector3& position, SegID segment, const Vector3& force);

    // Filter predicates

    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }

    void FixedUpdateObject(float dt, ObjID id, Object& obj);

    void CreateObjectDebris(const Object& obj, ModelID modelId, const Vector3& force);

    // Modifies an object's rotation to face towards a vector at a given rate per second
    void TurnTowardsDirection(Object& obj, Vector3 direction, float rate);

    // Modifies an object's rotation to face towards a point at a given rate per second
    void TurnTowardsPoint(Object& obj, const Vector3& target, float rate);

    // Similar to TurnTowardsVector but adds angular thrust, allowing overshoot
    void RotateTowards(Object& obj, Vector3 point, float angularThrust);

    void ApplyForce(Object& obj, const Vector3& force);
    void ApplyRotation(Object& obj, Vector3 force);

    namespace Game {
        Tuple<ObjRef, float> FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask);
        Tuple<ObjRef, float> FindNearestVisibleObject(const NavPoint& point, float maxDist, ObjectMask mask, span<ObjRef> objFilter);
        void AttachLight(const Object& obj, ObjRef ref);

        // Schedules an object to be added at end of update
        ObjRef AddObject(const Object& object);
        void FreeObject(ObjID id);
        void InitObjects(Inferno::Level& level);
        ObjRef DropPowerup(PowerupID pid, const Vector3& position, SegID segId, const Vector3& force = Vector3::Zero);
    }

    void DestroyObject(Object& obj);
    void ExplodeSubmodels(ModelID model, Object& obj);
    Tuple<ObjRef, float> FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask);

    // Returns true if object is a prox mine, smart mine or editor placed mine
    bool ObjectIsMine(const Object& obj);
}

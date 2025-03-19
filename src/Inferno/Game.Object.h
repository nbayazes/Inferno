#pragma once
#include "Level.h"
#include "Object.h"

namespace Inferno {
    struct SubmodelRef {
        short id = -1; // Submodel index. -1 is unset
        Vector3 offset; // Offset relative to submodel origin

        bool IsNull() const { return id == -1; }
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

    // Filter predicates

    inline bool IsReactor(const Object& obj) { return obj.Type == ObjectType::Reactor; }
    inline bool IsPlayer(const Object& obj) { return obj.Type == ObjectType::Player; }

    void FixedUpdateObject(float dt, ObjID id, Object& obj);

    void CreateObjectDebris(const Object& obj, ModelID modelId, const Vector3& force);

    // Modifies an object's rotation to face towards a vector at a given rate per second
    void TurnTowardsDirection(Object& obj, Vector3 direction, float rate);

    // Modifies an object's rotation to face towards a point at a given rate per second
    void TurnTowardsPoint(Object& obj, const Vector3& point, float rate);

    // Similar to TurnTowardsVector but adds angular thrust, allowing overshoot
    void RotateTowards(Object& obj, Vector3 point, float angularThrust);

    void ApplyForce(Object& obj, const Vector3& force);

    namespace Game {
        Tuple<ObjRef, float> FindNearestObject(const Vector3& position, float maxDist, ObjectMask mask);
        Tuple<ObjRef, float> FindNearestVisibleObject(const NavPoint& point, float maxDist, ObjectMask mask, span<ObjRef> objFilter);
        void AttachLight(const Object& obj, ObjRef ref);

        // Schedules an object to be added at end of update
        ObjRef AddObject(const Object& object);
        void FreeObject(ObjID id);
        void InitObjects(Inferno::Level& level);
        ObjRef DropPowerup(PowerupID pid, const Vector3& position, SegID segId, const Vector3& force = Vector3::Zero);

        void CloakObject(Object& obj, float duration, bool playSound = true);
        void UncloakObject(Object& obj, bool playSound = true);
        void MakeInvulnerable(Object& obj, float duration, bool playSound = true);
        void MakeVulnerable(Object& obj, bool playSound = true);
    }

    void CreateDebris(SegID segment, const Vector3& position, const Vector3& force = Vector3::Zero, float randomScale = 20);

    // Explodes an object and flags it as destroyed
    void DestroyObject(Object& obj);
    //void ExplodeSubmodels(ModelID model, Object& obj);

    // Returns true if object is a prox mine, smart mine or editor placed mine
    bool ObjectIsMine(const Object& obj);

    // Reloads various properties for the object from the game data.
    // The editor snapshots certain props such as health and it's best to refresh them.
    void InitObject(Object&, ObjectType type, int8 id = 0, bool fullReset = true);

    float GetObjectRadius(const Object& obj);

    // Returns a random direction on the object's forward plane
    Vector3 RandomLateralDirection(const Object& obj);
    SubmodelRef GetRandomPointOnObject(const Object& obj);

    // Tries to shift object away from intersecting walls
    void FixObjectPosition(Object& obj);

    AnimationState StartAnimation(const Object& object, const AnimationAngles& currentAngles, Animation anim, float time, float moveMult, float delay);

    void UpdateAnimation(AnimationAngles& angles, const Object& robot, AnimationState& state, float dt);

    // Teleports an object to a new segment
    void TeleportObject(Object& obj, SegID segid, const Vector3* position = nullptr, const Matrix3x3* rotation = nullptr, bool resetPhysics = true);

    // Returns true if the gunpoint is outside of the level.
    // distBuffer adds extra distance to the intersection range check
    bool GunpointIntersectsWall(const Object& obj, int8 gunpoint, float distBuffer = 0.5f);
}

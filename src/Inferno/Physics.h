#pragma once
#include "Level.h"
#include "DirectX.h"
#include "Intersect.h"

namespace Inferno {
    void UpdatePhysics(Level& level, const Room& room, double t, float dt);
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, bool isPlayer);

    namespace Debug {
        inline Vector3 ShipPosition, ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 120> ShipVelocities{};
        inline float Steps = 0, R = 0, K = 0;
        inline Vector3 ClosestPoint;
        inline List<Vector3> ClosestPoints;
        inline int SegmentsChecked = 0;
    };

    // Explosion that can cause damage or knockback
    struct GameExplosion {
        Vector3 Position;
        RoomID Room = RoomID::None;
        SegID Segment = SegID::None;

        float Radius;
        float Damage;
        float Force;
    };

    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion);
    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent);

    // Sets an object's angular velocity to turn towards a vector over a number of seconds.
    // Note that this is not additive, and overrides any existing angular velocity.
    void TurnTowardsVector(Object& obj, Vector3 towards, float rate);
    void ApplyForce(Object& obj, const Vector3& force);
    bool IntersectLevelDebris(Level& level, const DirectX::BoundingSphere&, SegID segId, LevelHit& hit);
}

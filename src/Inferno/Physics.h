#pragma once
#include "Level.h"
#include "Face.h"
#include "DirectX.h"
#include "Physics.Hit.h"

namespace Inferno {
    void UpdatePhysics(Level& level, double t, float dt);
    bool CheckDestroyableOverlay(Level& level, const Vector3& point, Tag tag, int tri, bool isPlayer);

    namespace Debug {
        inline Vector3 ShipPosition, ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 120> ShipVelocities{};
        inline float Steps = 0, R = 0, K = 0;
        inline Vector3 ClosestPoint;
        inline List<Vector3> ClosestPoints;
        inline int SegmentsChecked = 0;
    };

    struct LevelHit {
        Object* Source = nullptr;
        Tag Tag;
        Object* HitObj = nullptr;
        float Distance = FLT_MAX;
        float EdgeDistance = 0; // Impact distance from the face edge. Used for decal culling.
        Vector3 Point; // where the two objects or geometry touched
        Vector3 Normal, Tangent;
        int Tri = -1; // Triangle of the face hit. -1, 0 or 1
        float Speed = 0;
        bool Bounced = false;

        void Update(const HitInfo& hit, Object* obj) {
            if (!obj || hit.Distance > Distance) return;
            Distance = hit.Distance;
            Point = hit.Point;
            Normal = hit.Normal;
            HitObj = obj;
            Speed = hit.Speed;
            Tangent = hit.Normal.Cross(Vector3::Up);
            Tangent.Normalize();
            if (VectorNear(Tangent, { 0, 0, 0 }, 0.01f)) {
                Tangent = hit.Normal.Cross(Vector3::Right);
                Tangent.Normalize();
            }
        }

        void Update(const HitInfo& hit, struct Tag tag) {
            if (!tag || hit.Distance > Distance) return;
            Distance = hit.Distance;
            Point = hit.Point;
            Normal = hit.Normal;
            Tag = tag;
        }

        explicit operator bool() const { return Distance != FLT_MAX; }
    };

    // Explosion that can cause damage or knockback
    struct GameExplosion {
        Vector3 Position;
        RoomID Room;
        SegID Segment;

        float Radius;
        float Damage;
        float Force;
    };

    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion);
    bool IntersectRayLevel(Level& level, const Ray& ray, SegID start, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit& hit);
    bool IntersectRaySegment(Level& level, const Ray& ray, SegID segId, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit* hitResult = nullptr, float offset = 0);
    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent);

    // Sets an object's angular velocity to turn towards a vector over a number of seconds.
    // Note that this is not additive, and overrides any existing angular velocity.
    void TurnTowardsVector(Object& obj, Vector3 towards, float rate);
    void ApplyForce(Object& obj, const Vector3& force);
    bool IntersectLevelDebris(Level& level, const DirectX::BoundingSphere&, SegID segId, LevelHit& hit);
}

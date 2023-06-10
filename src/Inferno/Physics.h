#pragma once
#include "Level.h"
#include "Face.h"
#include "DirectX.h"
#include "Physics.Capsule.h"
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
        //float WallDistance = FLT_MAX; // ray distance to the wall
        float EdgeDistance = 0; // Impact distance from the face edge. Used for decal culling.
        Vector3 WallPoint; // point along the object's velocity vector where it hits a wall
        Vector3 Point; // where the two objects or geometry touched
        Vector3 Normal, Tangent;
        int Tri = -1; // Triangle of the face hit. -1, 0 or 1
        float Speed = 0;

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

        operator bool() const { return Distance != FLT_MAX; }
    };

    // Explosion that can cause damage or knockback
    struct GameExplosion {
        Vector3 Position;
        SegID Segment;

        float Radius;
        float Damage;
        float Force;

        //float Size;
        //float VClipRadius;
        //int VClip;
    };

    void CreateExplosion(Level& level, const Object* source, const GameExplosion& explosion);
    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit& hit);
    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent);
    bool IntersectLevelDebris(Level& level, const BoundingCapsule&, SegID segId, LevelHit& hit);
    HitInfo IntersectFaceSphere(const Face& face, const DirectX::BoundingSphere& sphere);
    HitInfo IntersectSphereSphere(const DirectX::BoundingSphere& a, const DirectX::BoundingSphere& b);
}

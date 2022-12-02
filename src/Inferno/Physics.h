#pragma once
#include "Level.h"
#include "Face.h"
#include "DirectX.h"

namespace Inferno {
    struct LevelHit;
    void WeaponHitWall(const LevelHit& hit, Object& obj, Level& level, ObjID objId);
    void UpdatePhysics(Level& level, double t, float dt);

    namespace Debug {
        inline Vector3 ShipPosition, ShipVelocity, ShipAcceleration, ShipThrust;
        inline Array<float, 120> ShipVelocities{};
        inline float Steps = 0, R = 0, K = 0;
        inline Vector3 ClosestPoint;
        inline List<Vector3> ClosestPoints;
    };

    struct HitInfo {
        float Distance = FLT_MAX;
        Vector3 Point, Normal;
        operator bool() const { return Distance != FLT_MAX; }
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
        Set<SegID> Visited; // visited segments
        int Tri = -1; // Triangle of the face hit. -1, 0 or 1

        void Update(const HitInfo& hit, Object* obj) {
            if (!obj || hit.Distance > Distance) return;
            Distance = hit.Distance;
            Point = hit.Point;
            Normal = hit.Normal;
            HitObj = obj;
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

    //Vector3 ClosestPointOnLine(const Vector3& a, const Vector3& b, const Vector3& p);
    //HitInfo IntersectSphereSphere(const DirectX::BoundingSphere& a, const DirectX::BoundingSphere& b);

    //struct ClosestResult { float distSq, s, t; Vector3 c1, c2; };
    //ClosestResult ClosestPointBetweenLines(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2);
    //bool PointInTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point);

    struct BoundingCapsule {
        Vector3 A, B;
        float Radius;

        HitInfo Intersects(const DirectX::BoundingSphere& sphere) const;
        bool Intersects(const BoundingCapsule& other) const;
        bool Intersects(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& faceNormal, Vector3& refPoint, Vector3& normal, float& dist) const;
    };

    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, bool passTransparent, bool hitTestTextures, LevelHit& hit);
    HitInfo IntersectFaceSphere(const Face& face, const DirectX::BoundingSphere& sphere);
    bool IntersectLevelDebris(Level& level, const BoundingCapsule&, SegID segId, LevelHit& hit);
    bool ObjectToObjectVisibility(const Object& a, const Object& b, bool passTransparent);
}

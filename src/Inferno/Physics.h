#pragma once
#include "Level.h"
#include "Face.h"

namespace Inferno {
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
        operator bool() { return Distance != FLT_MAX; }
    };

    struct LevelHit {
        Object* Source = nullptr;
        Tag Tag;
        Object* HitObj = nullptr;
        float Distance = FLT_MAX;
        Vector3 Point, Normal;
        Set<SegID> Visited; // visited segments

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

        operator bool() { return Distance != FLT_MAX; }
    };

    bool IntersectLevel(Level& level, const Ray& ray, SegID start, float maxDist, LevelHit& hit);
    HitInfo IntersectFaceSphere(const Face& face, const DirectX::BoundingSphere& sphere);
}

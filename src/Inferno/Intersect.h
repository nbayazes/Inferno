#pragma once
#include "Face.h"

namespace Inferno {
    struct HitInfo {
        float Distance = FLT_MAX; // How far the hit was from the starting point
        Vector3 Point; // Where the intersection happened
        Vector3 Normal; // The normal of the intersection
        int16 Tri = -1; // What triangle was hit (for level walls) (unused?)
        float Speed = 0;
        explicit operator bool() const { return Distance != FLT_MAX; }
    };

    // Returns the nearest intersection point on a face
    HitInfo IntersectFaceSphere(const Face& face, const DirectX::BoundingSphere& sphere);
    HitInfo IntersectSphereSphere(const DirectX::BoundingSphere& a, const DirectX::BoundingSphere& b);
    HitInfo IntersectPointSphere(const Vector3& point, const DirectX::BoundingSphere& sphere);

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


    Vector3 ClosestPointOnLine(const Vector3& a, const Vector3& b, const Vector3& p);
    // Returns true if a point lies within a triangle
    bool TriangleContainsPoint(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point);

    bool TriangleContainsPoint(const Array<Vector3, 3>& tri, Vector3 point);

    bool FaceContainsPoint(const Face& face, const Vector3& point);

    // Returns the closest point on a triangle to a point
    Vector3 ClosestPointOnTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point);

    // Returns the closes point and distance on a triangle to a point
    Tuple<Vector3, float> ClosestPointOnTriangle2(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& point, int* edgeIndex = nullptr);

    // Returns the nearest distance to the face edge and a point. Skips the internal split.
    float FaceEdgeDistance(const Segment& seg, SideID side, const Face2& face, const Vector3& point);

    // Wraps a UV value to 0-1
    void WrapUV(Vector2& uv);

    // Returns the UVs on a face closest to a point in world coordinates
    Vector2 IntersectFaceUVs(const Vector3& point, const Face2& face, int tri);
    void FixOverlayRotation(uint& x, uint& y, int width, int height, OverlayRotation rotation);

    // Returns true if the point was transparent
    bool WallPointIsTransparent(const Vector3& pnt, const Face2& face, int tri);

    enum class RayQueryMode {
        Visibility, // Ignores walls that have transparent textures
        Precise, // Hit-tests transparent textures
        IgnoreWalls, // Ignores all walls
    };

    struct RayQuery {
        float MaxDistance = 0; // Max distance the ray can travel
        SegID Start = SegID::None; // Segment the ray starts in
        RayQueryMode Mode = RayQueryMode::Visibility;
    };

    bool IntersectRaySegment(Level& level, const Ray& ray, SegID segId, float maxDist);

    // Returns the segment side hit by a ray. Returns SideID::None if the ray is outside the segment or too far.
    SideID IntersectRaySegmentSide(Level& level, const Ray& ray, Tag tag, float maxDist);

    class IntersectContext {
        List<SegID> _visitedSegs;
        const Level* _level;
    public:
        IntersectContext(const Level& level) : _level(&level) {}

        // intersects a ray with the level, returning hit information. Also tests against object spheres if mask is set.
        bool RayLevel(Ray ray, const RayQuery& query, LevelHit& hit, ObjectMask mask = ObjectMask::None, ObjID source = ObjID::None);
    };

    namespace Debug {
        inline Vector3 RayStart, RayEnd;
    }
}

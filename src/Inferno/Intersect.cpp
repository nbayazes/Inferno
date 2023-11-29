#include "pch.h"
#include "Intersect.h"

#include "Game.Segment.h"
#include "Game.Wall.h"
#include "Resources.h"
#include "Utility.h"
#include "Segment.h"

namespace Inferno {
    using DirectX::BoundingSphere;

    HitInfo IntersectFaceSphere(const Face& face, const DirectX::BoundingSphere& sphere) {
        HitInfo hit;
        auto& i = face.Side.GetRenderIndices();

        if (sphere.Intersects(face[i[0]], face[i[1]], face[i[2]])) {
            auto p = ClosestPointOnTriangle(face[i[0]], face[i[1]], face[i[2]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
                hit.Tri = 0;
            }
        }

        if (sphere.Intersects(face[i[3]], face[i[4]], face[i[5]])) {
            auto p = ClosestPointOnTriangle(face[i[3]], face[i[4]], face[i[5]], sphere.Center);
            auto dist = (p - sphere.Center).Length();
            if (dist < hit.Distance) {
                hit.Point = p;
                hit.Distance = dist;
                hit.Tri = 1;
            }
        }

        if (hit.Distance > sphere.Radius)
            hit.Distance = FLT_MAX;
        else
            (hit.Point - sphere.Center).Normalize(hit.Normal);

        return hit;
    }


    // intersects a with b, with hit normal pointing towards a
    HitInfo IntersectSphereSphere(const BoundingSphere& a, const BoundingSphere& b) {
        HitInfo hit;
        Vector3 c0(a.Center), c1(b.Center);
        auto v = c0 - c1;
        auto distance = v.Length();
        if (distance < a.Radius + b.Radius) {
            v.Normalize();
            hit.Point = b.Center + v * b.Radius;
            hit.Distance = Vector3::Distance(hit.Point, c0);
            hit.Normal = v;
        }

        return hit;
    }

    // Intersects a sphere with a point. Surface normal points towards point.
    HitInfo IntersectPointSphere(const Vector3& point, const BoundingSphere& sphere) {
        HitInfo hit;
        auto dir = point - sphere.Center;
        float depth = sphere.Radius - dir.Length();
        if (depth > 0) {
            dir.Normalize();
            hit.Point = sphere.Center + dir * sphere.Radius;
            hit.Distance = Vector3::Distance(hit.Point, point);
            hit.Normal = -dir;
        }

        return hit;
    }

    Vector3 ClosestPointOnLine(const Vector3& a, const Vector3& b, const Vector3& p) {
        // Project p onto ab, computing the paramaterized position d(t) = a + t * (b - a)
        auto ab = b - a;
        auto t = (p - a).Dot(ab) / ab.Dot(ab);

        // Clamp T to a 0-1 range. If t was < 0 or > 1 then the closest point was outside the line!
        t = std::clamp(t, 0.0f, 1.0f);

        // Compute the projected position from the clamped t
        return a + t * ab;
    }

    bool TriangleContainsPoint(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point) {
        // Move the triangle so that the point becomes the triangle's origin
        auto a = p0 - point;
        auto b = p1 - point;
        auto c = p2 - point;

        // Compute the normal vectors for triangles:
        Vector3 u = b.Cross(c), v = c.Cross(a), w = a.Cross(b);

        // Test if the normals are facing the same direction
        return u.Dot(v) >= 0.0f && u.Dot(w) >= 0.0f && v.Dot(w) >= 0.0f;
    }

    bool TriangleContainsPoint(const Array<Vector3, 3>& tri, Vector3 point) {
        // Move the triangle so that the point becomes the triangle's origin
        auto a = tri[0] - point;
        auto b = tri[1] - point;
        auto c = tri[2] - point;

        // Compute the normal vectors for triangles:
        Vector3 u = b.Cross(c), v = c.Cross(a), w = a.Cross(b);

        // Test if the normals are facing the same direction
        return u.Dot(v) >= 0.0f && u.Dot(w) >= 0.0f && v.Dot(w) >= 0.0f;
    }

    bool FaceContainsPoint(const Face& face, const Vector3& point) {
        return TriangleContainsPoint(face[0], face[1], face[2], point) || TriangleContainsPoint(face[2], face[3], face[0], point);
    }

    Vector3 ClosestPointOnTriangle(const Vector3& p0, const Vector3& p1, const Vector3& p2, Vector3 point) {
        Plane plane(p0, p1, p2);
        point = ProjectPointOntoPlane(point, plane);

        if (TriangleContainsPoint(p0, p1, p2, point))
            return point; // point is on the surface of the triangle

        // check the points and edges
        auto c1 = ClosestPointOnLine(p0, p1, point);
        auto c2 = ClosestPointOnLine(p1, p2, point);
        auto c3 = ClosestPointOnLine(p2, p0, point);

        auto mag1 = (point - c1).LengthSquared();
        auto mag2 = (point - c2).LengthSquared();
        auto mag3 = (point - c3).LengthSquared();

        float min = std::min(std::min(mag1, mag2), mag3);

        if (min == mag1)
            return c1;
        else if (min == mag2)
            return c2;
        return c3;
    }

    Tuple<Vector3, float> ClosestPointOnTriangle2(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& point, int* edgeIndex) {
        Vector3 points[3] = {
            ClosestPointOnLine(p0, p1, point),
            ClosestPointOnLine(p1, p2, point),
            ClosestPointOnLine(p2, p0, point)
        };

        float distances[3]{};
        for (int j = 0; j < std::size(points); j++) {
            distances[j] = Vector3::Distance(point, points[j]);
        }

        int minIndex = 0;
        for (int j = 0; j < std::size(points); j++) {
            if (distances[j] < distances[minIndex])
                minIndex = j;
        }

        if (edgeIndex) *edgeIndex = minIndex;

        return { points[minIndex], distances[minIndex] };
    }


    float FaceEdgeDistance(const Segment& seg, SideID side, const Face2& face, const Vector3& point) {
        // Check the four outside edges of the face
        float mag1 = FLT_MAX, mag2 = FLT_MAX, mag3 = FLT_MAX, mag4 = FLT_MAX;

        // todo: this isn't true for inverted segments
        // If the edge doesn't have a connection it's safe to put a decal on it
        if (seg.SideHasConnection(GetAdjacentSide(side, 0))) {
            auto c = ClosestPointOnLine(face[0], face[1], point);
            mag1 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 1))) {
            auto c = ClosestPointOnLine(face[1], face[2], point);
            mag2 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 2))) {
            auto c = ClosestPointOnLine(face[2], face[3], point);
            mag3 = (point - c).Length();
        }
        if (seg.SideHasConnection(GetAdjacentSide(side, 3))) {
            auto c = ClosestPointOnLine(face[3], face[0], point);
            mag4 = (point - c).Length();
        }

        return std::min(std::min(std::min(mag1, mag2), mag3), mag4);
    }

    void WrapUV(Vector2& uv) {
        float rmx{};
        if (uv.x < 0)
            uv.x = std::abs(std::modf(uv.x, &rmx));

        uv.x = std::fmodf(uv.x, 1);

        if (uv.y < 0)
            uv.y = std::abs(std::modf(uv.y, &rmx));

        uv.y = std::fmodf(uv.y, 1);
    }

    Vector2 IntersectFaceUVs(const Vector3& point, const Face2& face, int tri) {
        auto& indices = face.Side->GetRenderIndices();
        auto& v0 = face[indices[tri * 3 + 0]];
        auto& v1 = face[indices[tri * 3 + 1]];
        auto& v2 = face[indices[tri * 3 + 2]];

        Vector2 uvs[3]{};
        for (int i = 0; i < 3; i++)
            uvs[i] = face.Side->UVs[indices[tri * 3 + i]];

        // Vectors of two edges
        auto xAxis = v1 - v0;
        xAxis.Normalize();
        auto zAxis = xAxis.Cross(v2 - v0);
        zAxis.Normalize();
        auto yAxis = xAxis.Cross(zAxis);

        // Project triangle to 2D
        Vector2 z0(0, 0);
        Vector2 z1((v1 - v0).Length(), 0);
        Vector2 z2((v2 - v0).Dot(xAxis), (v2 - v0).Dot(yAxis));
        Vector2 hit((point - v0).Dot(xAxis), (point - v0).Dot(yAxis)); // project point onto plane

        // barycentric coords of hit
        auto bx = (z1 - z0).Cross(hit - z0).x;
        auto by = (z2 - z1).Cross(hit - z1).x;
        auto bz = (z0 - z2).Cross(hit - z2).x;
        auto ba = Vector3(bx, by, bz) / (bx + by + bz);

        return Vector2::Barycentric(uvs[1], uvs[2], uvs[0], ba.x, ba.y);
    }

    void FixOverlayRotation(uint& x, uint& y, int width, int height, OverlayRotation rotation) {
        uint t = 0;

        switch (rotation) // adjust for overlay rotation
        {
            case OverlayRotation::Rotate0:
                break;
            case OverlayRotation::Rotate90:
                t = y;
                y = x;
                x = width - t - 1;
                break;
            case OverlayRotation::Rotate180:
                y = height - y - 1;
                x = width - x - 1;
                break;
            case OverlayRotation::Rotate270:
                t = x;
                x = y;
                y = height - t - 1;
                break;
        }
    }

    bool WallPointIsTransparent(const Vector3& pnt, const Face2& face, int tri) {
        auto& side = *face.Side;
        auto tmap = side.TMap2 > LevelTexID::Unset ? side.TMap2 : side.TMap;
        auto& bitmap = Resources::GetBitmap(Resources::LookupTexID(tmap));
        if (!bitmap.Info.Transparent) return false; // Must be flagged transparent

        auto uv = IntersectFaceUVs(pnt, face, tri);
        auto wrap = [](float x, uint16 size) {
            // -1 so that x = 1.0 results in width - 1, correcting for the array index
            return (uint)Mod(uint16(x * (float)size - 1.0f), size);
        };

        auto& info = bitmap.Info;
        auto x = wrap(uv.x, info.Width);
        auto y = wrap(uv.y, info.Height);

        // for overlay textures, check the supertransparent mask
        if (side.TMap2 > LevelTexID::Unset) {
            FixOverlayRotation(x, y, info.Width, info.Height, side.OverlayRotation);
            const int idx = y * info.Width + x;
            if (!bitmap.Mask.empty() && bitmap.Mask[idx] == Palette::SUPER_MASK)
                return true; // supertransparent overlay

            if (bitmap.Data[idx].a != 0)
                return false; // overlay wasn't transparent

            // Check the base texture
            auto& tmap1 = Resources::GetBitmap(Resources::LookupTexID(side.TMap));
            x = wrap(uv.x, info.Width);
            y = wrap(uv.y, info.Height);
            return tmap1.Data[idx].a == 0;
        }
        else {
            return bitmap.Data[y * info.Width + x].a == 0;
        }
    }

    bool IntersectContext::RayLevel(const Ray& ray, const RayQuery& query, LevelHit& hit, ObjectMask mask, ObjID source) {
        ASSERT(query.Start != SegID::None); // Very bad for perf to not supply seg
        ASSERT(query.MaxDistance > 0);
        if (query.MaxDistance <= 0.01f) return false;

        auto next = TraceSegment(*_level, query.Start, ray.position); // Check that the ray is inside the segment
        _visitedSegs.clear();
        _visitedSegs.reserve(10);

        while (next > SegID::None) {
            SegID segId = next;
            _visitedSegs.push_back(segId); // must track visited segs to prevent circular logic
            next = SegID::None;
            auto& seg = _level->GetSegment(segId);

            if (mask != ObjectMask::None) {
                for (auto& objid : seg.Objects) {
                    if (source == objid) continue;
                    if (auto obj = _level->TryGetObject(objid)) {
                        if (!obj->IsAlive()) continue;
                        if (!obj->PassesMask(mask)) continue;

                        BoundingSphere sphere(obj->Position, obj->Radius);
                        float dist;
                        if (ray.Intersects(sphere, dist) && dist < query.MaxDistance)
                            return true; // Intersected an object
                    }
                }
            }

            for (auto& side : SideIDs) {
                auto face = Face2::FromSide(*_level, seg, side);

                float dist{};
                auto tri = face.Intersects(ray, dist);
                if (tri != -1 && dist < hit.Distance) {
                    if (dist > query.MaxDistance) return {}; // hit is too far

                    auto intersect = hit.Point = ray.position + ray.direction * dist;
                    Tag tag{ segId, side };

                    bool isSolid = !seg.SideHasConnection(side);

                    if (!query.IgnoreWalls) {
                        if (auto wall = _level->TryGetWall(tag)) {
                            if (!wall->IsSolid())
                                isSolid = false;
                            else if (query.TestTextures)
                                isSolid = !WallPointIsTransparent(intersect, face, tri);
                        }
                    }

                    if (isSolid) {
                        hit.Tag = tag;
                        hit.Distance = dist;
                        hit.Normal = face.AverageNormal();
                        hit.Tangent = face.Side->Tangents[tri];
                        hit.Point = ray.position + ray.direction * dist;
                        hit.EdgeDistance = FaceEdgeDistance(seg, side, face, hit.Point);
                        return true;
                    }
                    else {
                        auto conn = seg.GetConnection(side);
                        if (!Seq::contains(_visitedSegs, conn))
                            next = conn;
                        break; // go to next segment
                    }
                }
            }
        }

        return false;
    }

    // intersects a ray with the level, returning hit information
    bool IntersectRayLevel(Level& level, const Ray& ray, const RayQuery& query, LevelHit& hit) {
        SegID next = query.Start;
        if (next == SegID::None)
            next = FindContainingSegment(level, ray.position);

        if (next == SegID::None) return false;
        if (query.MaxDistance <= 0.01f) return false;

        List<SegID> visitedSegs;
        visitedSegs.clear();
        visitedSegs.reserve(10);

        while (next > SegID::None) {
            SegID segId = next;
            visitedSegs.push_back(segId); // must track visited segs to prevent circular logic
            next = SegID::None;
            auto& seg = level.GetSegment(segId);

            for (auto& side : SideIDs) {
                auto face = Face2::FromSide(level, seg, side);

                float dist{};
                auto tri = face.Intersects(ray, dist);
                if (tri != -1 && dist < hit.Distance) {
                    if (dist > query.MaxDistance) return {}; // hit is too far

                    auto intersect = hit.Point = ray.position + ray.direction * dist;
                    Tag tag{ segId, side };

                    bool isSolid = !seg.SideHasConnection(side);

                    if (!query.IgnoreWalls) {
                        if (auto wall = level.TryGetWall(tag)) {
                            if (!wall->IsSolid())
                                isSolid = false;
                            else if (query.TestTextures)
                                isSolid = !WallPointIsTransparent(intersect, face, tri);
                        }
                    }

                    if (isSolid) {
                        hit.Tag = tag;
                        hit.Distance = dist;
                        hit.Normal = face.AverageNormal();
                        hit.Tangent = face.Side->Tangents[tri];
                        hit.Point = ray.position + ray.direction * dist;
                        hit.EdgeDistance = FaceEdgeDistance(seg, side, face, hit.Point);
                        return true;
                    }
                    else {
                        auto conn = seg.GetConnection(side);
                        if (!Seq::contains(visitedSegs, conn))
                            next = conn;
                        break; // go to next segment
                    }
                }
            }
        }

        return false;
    }

    SideID IntersectRaySegmentSide(Level& level, const Ray& ray, Tag tag, float maxDist) {
        auto seg = level.TryGetSegment(tag);
        if (!seg) return SideID::None;

        auto face = Face2::FromSide(level, *seg, tag.Side);
        float dist{};
        auto tri = face.Intersects(ray, dist);
        if (tri == -1 || dist > maxDist) return SideID::None;
        return tag.Side;
    }

    bool IntersectRaySegment(Level& level, const Ray& ray, SegID segId, float maxDist) {
        auto seg = level.TryGetSegment(segId);
        if (!seg) return false;

        for (auto& side : SideIDs) {
            if (!seg->SideIsSolid(side, level)) continue;
            auto face = Face2::FromSide(level, *seg, side);

            float dist{};
            auto tri = face.Intersects(ray, dist);
            if (tri == -1 || dist > maxDist) continue; // hit is too far

            bool isSolid = !seg->SideHasConnection(side);

            if (auto wall = level.TryGetWall(face.Side->Wall))
                isSolid = wall->IsSolid();

            if (isSolid)
                return true;
        }

        return false;
    }
}

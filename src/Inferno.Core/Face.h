#pragma once
#include "Utility.h"
#include "Level.h"

namespace Inferno {
    struct FaceHit {
        float Distance;
        Vector3 Normal;
    };

    // Helper to perform operations on a segment face. A face is always 4 points.
    // Do not store long term as it contains references and not a copy.
    struct Face {
        Vector3 &P0, &P1, &P2, &P3;
        SegmentSide& Side;
        Array<PointID, 4> Indices;

        Face(Vector3& p0, Vector3& p1, Vector3& p2, Vector3& p3, SegmentSide& side, Array<PointID, 4> indices) :
            P0(p0), P1(p1), P2(p2), P3(p3), Side(side), Indices(indices) { }

        static Face FromSide(Level& level, SegID segId, SideID side) {
            auto& seg = level.GetSegment(segId);
            return FromSide(level, seg, side);
        }

        static Face FromSide(Level& level, Tag tag) {
            return FromSide(level, tag.Segment, tag.Side);
        }

        static Face FromSide(Level& level, Segment& seg, SideID side) {
            auto& sideVerts = SIDE_INDICES[(int)side];
            auto& v0 = level.Vertices[seg.Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[seg.Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[seg.Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[seg.Indices[sideVerts[3]]];
            return Face(v0, v1, v2, v3, seg.GetSide(side), seg.GetVertexIndices(side));
        }

        // Returns -1 if no hit, 0 if hit tri 0, 1 if hit tri 1
        int Intersects(const Ray& ray, float& dist, bool hitBackface = false) const {
            // bug: if the ray is directly on top of the triangle this returns false
            auto& i = Side.GetRenderIndices();
            bool hitTri0 = hitBackface || Side.Normals[0].Dot(ray.direction) < 0;
            bool hitTri1 = hitBackface || Side.Normals[1].Dot(ray.direction) < 0;
            if (hitTri0 && ray.Intersects(GetPoint(i[0]), GetPoint(i[1]), GetPoint(i[2]), dist))
                return 0;

            if (hitTri1 && ray.Intersects(GetPoint(i[3]), GetPoint(i[4]), GetPoint(i[5]), dist))
                return 1;

            return -1;
        }

        // Returns -1 if no hit, 0 if hit tri 0, 1 if hit tri 1
        int IntersectsOffset(const Ray& ray, float& dist, float offset, bool hitBackface = false) const {
            auto& i = Side.GetRenderIndices();
            bool hitTri0 = hitBackface || Side.Normals[0].Dot(ray.direction) < 0;
            bool hitTri1 = hitBackface || Side.Normals[1].Dot(ray.direction) < 0;

            auto offset0 = Side.Normals[0] * offset;
            if (hitTri0 && ray.Intersects(GetPoint(i[0]) + offset0, GetPoint(i[1]) + offset0, GetPoint(i[2]) + offset0, dist))
                return 0;

            auto offset1 = Side.Normals[1] * offset;
            if (hitTri1 && ray.Intersects(GetPoint(i[3]) + offset1, GetPoint(i[4]) + offset1, GetPoint(i[5]) + offset1, dist))
                return 1;

            return -1;
        }

        Array<Vector3, 4> CopyPoints() const {
            return { P0, P1, P2, P3 };
        }

        Vector3& operator [](int index) const {
            assert(index >= 0);
            switch (index % 4) {
                default:
                case 0: return P0;
                case 1: return P1;
                case 2: return P2;
                case 3: return P3;
            }
        }

        Vector3& GetPoint(int index) { return (*this)[index]; }
        const Vector3& GetPoint(int index) const { return (*this)[index]; }

        // Returns point 0-3
        int16 GetClosestPoint(const Vector3& pos) const {
            int16 closest = 0;
            auto maxDist = FLT_MAX;
            for (int16 i = 0; i < 4; i++) {
                if (auto dist = Vector3::Distance(GetPoint(i), pos); dist < maxDist) {
                    maxDist = dist;
                    closest = i;
                }
            }

            return closest;
        }

        // Returns point 0-3
        int16 GetClosestEdge(const Vector3& pos) const {
            int16 closest = 0;
            auto maxDist = FLT_MAX;
            for (int16 i = 0; i < 4; i++) {
                auto midpoint = GetEdgeMidpoint(i);
                if (auto dist = Vector3::Distance(midpoint, pos); dist < maxDist) {
                    maxDist = dist;
                    closest = i;
                }
            }

            return closest;
        }

        Vector3 AverageNormal() const {
            return Side.AverageNormal;
        }

        float Distance(const Vector3& point) const {
            Plane p(Center(), Side.AverageNormal);
            return p.DotCoordinate(point);
        }

        float Distance(const Vector3& point, uint edge) const {
            Plane p(Side.CenterForEdge(edge), Side.NormalForEdge(edge));
            return p.DotCoordinate(point);
        }

        Vector3 Center() const {
            return Side.Center;
        }

        Tuple<Vector3&, Vector3&> VerticesForEdge(uint edge) {
            auto& p0 = GetPoint(edge);
            auto& p1 = GetPoint(edge + 1);
            return { p0, p1 };
        }

        // Makes a copy of verts!
        Array<Vector3, 3> GetPoly(int index) const {
            ASSERT(index == 0 || index == 1);
            auto i = index * 3;
            auto& indices = Side.GetRenderIndices();
            return { GetPoint(indices[0 + i]), GetPoint(indices[1 + i]), GetPoint(indices[2 + i]) };
        }

        Plane GetPlane(int index) {
            ASSERT(index == 0 || index == 1);
            auto& indices = Side.GetRenderIndices();
            auto i = index * 3;
            return Plane(GetPoint(indices[0 + i]), GetPoint(indices[1 + i]), GetPoint(indices[2 + i]));
        }

        Vector3 VectorForEdge(uint edge) {
            auto [p0, p1] = VerticesForEdge(edge);
            auto p = p1 - p0;
            p.Normalize();
            return p;
        }

        // Gets the UV vector for an edge
        Vector2 VectorForEdgeUV(uint edge) const {
            auto v = Side.UVs[(edge + 1) % 4] - Side.UVs[edge % 4];
            v.Normalize();
            return v;
        }

        Vector3 GetEdgeMidpoint(uint edge) const {
            auto& p0 = GetPoint(edge);
            auto& p1 = GetPoint(edge + 1);
            return (p0 + p1) / 2;
        }

        float Area() const {
            auto v1 = P1 - P0;
            auto v2 = P3 - P0;
            return v1.Cross(v2).Length();
        }

        float GetAngleBetween(const Face& face) const {
            auto dir = face.Center() - Center();
            dir.Normalize();
            auto dot = AverageNormal().Dot(dir);
            return acos(dot) * RadToDeg;
        }

        // Check if a face lies directly on top of another face. Ignores vertex order.
        bool Overlaps(const Face& face, float tolerance = 0.01f) const {
            bool overlaps = true;

            for (int i = 0; i < 4; i++) {
                auto& p = GetPoint(i);

                bool pointOverlap = false;
                for (int j = 0; j < 4; j++) {
                    auto& other = face[j];
                    auto vdist = Vector3::Distance(p, other);
                    if (vdist < tolerance)
                        pointOverlap = true;
                }

                overlaps &= pointOverlap; // will evaluate false if no points match
            }

            return overlaps;
        }

        bool SharesIndices(const Face& face) const {
            for (auto& i : face.Indices) {
                for (auto& j : Indices) {
                    if (i == j) return true;
                }
            }

            return false;
        }

        float FlatnessRatio() const {
            auto GetRatio = [this](int i0, int i1, int i2, int i3) {
                auto length1 = PointToLineDistance(GetPoint(i0), GetPoint(i1), GetPoint(i2));
                auto length2 = PointToLineDistance(GetPoint(i0), GetPoint(i1), GetPoint(i3));
                auto ave_length = (length1 + length2) / 2;
                auto midpoint1 = (GetPoint(i0) + GetPoint(i1)) / 2;
                auto midpoint2 = (GetPoint(i2) + GetPoint(i3)) / 2;
                auto mid_length = (midpoint2 - midpoint1).Length();
                return mid_length / ave_length;
            };

            auto ratio1 = GetRatio(0, 1, 2, 3);
            auto ratio2 = GetRatio(1, 2, 3, 0);
            return std::min(ratio1, ratio2);
        }

        void Reflect(span<Vector3> points) const {
            Plane plane(Center(), AverageNormal());
            auto reflect = Matrix::CreateReflection(plane);

            for (auto& point : points)
                point = Vector3::Transform(point, reflect);
        }

        // Insets the vertices towards the center of the face by distance, and offsets along the normal by height.
        Array<Vector3, 4> Inset(float distance, float height) const {
            Array<Vector3, 4> points = CopyPoints();
            for (int i = 0; i < points.size(); i++) {
                // Move towards center of face
                auto vectorToCenter = Center() - points[i];
                vectorToCenter.Normalize();
                points[i] += vectorToCenter * distance + AverageNormal() * height;
            }
            return points;
        }

        // Insets each edge using tangent vectors. Maintains an exact distance from each side.
        Array<Vector3, 4> InsetTangent(float distance, float height) const {
            Array<Vector3, 4> points = CopyPoints();
            for (int i = 0; i < points.size(); i++) {
                auto tangent = GetPoint(i + 1) - GetPoint(i);
                auto bitangent = GetPoint(i + 3) - GetPoint(i);
                tangent.Normalize();
                bitangent.Normalize();
                points[i] += tangent * distance + bitangent * distance + AverageNormal() * height;
            }
            return points;
        }

        // Returns the index of the longest edge
        PointID GetLongestEdge() {
            float indexLen = 0;
            int index = 0;

            for (int i = 0; i < 4; i++) {
                auto len = (GetPoint(i + 1) - GetPoint(i)).LengthSquared();
                if (len > indexLen) {
                    index = i;
                    indexLen = len;
                }
            }

            return (PointID)index;
        }

        PointID GetShortestEdge() {
            float indexLen = FLT_MAX;
            int index = 0;

            for (int i = 0; i < 4; i++) {
                auto len = (GetPoint(i + 1) - GetPoint(i)).LengthSquared();
                if (len < indexLen) {
                    index = i;
                    indexLen = len;
                }
            }

            return (PointID)index;
        }

        // Returns true if any points of the face are in front of a plane
        bool InFrontOfPlane(const Plane& plane, float offset = 0) {
            Vector3 poffset;
            if (offset != 0) poffset += AverageNormal() * offset;
            for (int i = 0; i < 4; i++) {
                auto dist = plane.DotCoordinate(GetPoint(i) + poffset);
                if (dist > 0)
                    return true;
            }

            return false;
        }
    };

    struct Face2 {
        Array<Vector3, 4> Points;
        const SegmentSide* Side;
        Array<PointID, 4> Indices;

        Face2(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, const SegmentSide& side, Array<PointID, 4> indices) :
            Points({ p0, p1, p2, p3 }), Side(&side), Indices(indices) {}

        static Face2 FromSide(const Level& level, SegID segId, SideID side) {
            auto& seg = level.GetSegment(segId);
            return FromSide(level, seg, side);
        }

        static Face2 FromSide(const Level& level, Tag tag) {
            return FromSide(level, tag.Segment, tag.Side);
        }

        static Face2 FromSide(const Level& level, const Segment& seg, SideID side) {
            auto& sideVerts = SIDE_INDICES[(int)side];
            auto& v0 = level.Vertices[seg.Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[seg.Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[seg.Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[seg.Indices[sideVerts[3]]];
            return Face2(v0, v1, v2, v3, seg.GetSide(side), seg.GetVertexIndices(side));
        }

        const Vector3& operator [](int index) const {
            assert(index >= 0);
            return Points[index % 4];
        }

        // Returns -1 if no hit, 0 if hit tri 0, 1 if hit tri 1
        int Intersects(const Ray& ray, float& dist, bool hitBackface = false) const {
            // bug: if the ray is directly on top of the triangle this returns false
            auto& i = Side->GetRenderIndices();
            bool hitTri0 = hitBackface || Side->Normals[0].Dot(ray.direction) < 0;
            bool hitTri1 = hitBackface || Side->Normals[1].Dot(ray.direction) < 0;
            if (hitTri0 && ray.Intersects(Points[i[0]], Points[i[1]], Points[i[2]], dist))
                return 0;

            if (hitTri1 && ray.Intersects(Points[i[3]], Points[i[4]], Points[i[5]], dist))
                return 1;

            return -1;
        }

        // Returns -1 if no hit, 0 if hit tri 0, 1 if hit tri 1
        int IntersectsOffset(const Ray& ray, float& dist, float offset, bool hitBackface = false) const {
            auto& i = Side->GetRenderIndices();
            bool hitTri0 = hitBackface || Side->Normals[0].Dot(ray.direction) < 0;
            bool hitTri1 = hitBackface || Side->Normals[1].Dot(ray.direction) < 0;

            auto offset0 = Side->Normals[0] * offset;
            if (hitTri0 && ray.Intersects(Points[i[0]] + offset0, Points[i[1]] + offset0, Points[i[2]] + offset0, dist))
                return 0;

            auto offset1 = Side->Normals[1] * offset;
            if (hitTri1 && ray.Intersects(Points[i[3]] + offset1, Points[i[4]] + offset1, Points[i[5]] + offset1, dist))
                return 1;

            return -1;
        }

        // Returns point 0-3
        int16 GetClosestPoint(const Vector3& pos) const {
            int16 closest = 0;
            auto maxDist = FLT_MAX;
            for (int16 i = 0; i < 4; i++) {
                if (auto dist = Vector3::Distance(Points[i], pos); dist < maxDist) {
                    maxDist = dist;
                    closest = i;
                }
            }

            return closest;
        }

        // Returns point 0-3
        int16 GetClosestEdge(const Vector3& pos) const {
            int16 closest = 0;
            auto maxDist = FLT_MAX;
            for (int16 i = 0; i < 4; i++) {
                auto midpoint = GetEdgeMidpoint(i);
                if (auto dist = Vector3::Distance(midpoint, pos); dist < maxDist) {
                    maxDist = dist;
                    closest = i;
                }
            }

            return closest;
        }

        Vector3 AverageNormal() const {
            return Side->AverageNormal;
        }

        float Distance(const Vector3& point) const {
            Plane p(Center(), Side->AverageNormal);
            return p.DotCoordinate(point);
        }

        float Distance(const Vector3& point, uint edge) const {
            Plane p(Side->CenterForEdge(edge), Side->NormalForEdge(edge));
            return p.DotCoordinate(point);
        }

        Vector3 Center() const {
            return Side->Center;
        }

        Tuple<Vector3, Vector3> VerticesForEdge(uint edge) const {
            auto& p0 = Points[edge];
            auto& p1 = Points[(edge + 1) % 4];
            return { p0, p1 };
        }

        Array<Vector3, 3> GetPoly(int index) const {
            ASSERT(index == 0 || index == 1);
            auto i = index * 3;
            auto& indices = Side->GetRenderIndices();
            return { Points[indices[0 + i]], Points[indices[1 + i]], Points[indices[2 + i]] };
        }

        Plane GetPlane(int index) const {
            ASSERT(index == 0 || index == 1);
            auto& indices = Side->GetRenderIndices();
            auto i = index * 3;
            return Plane(Points[indices[0 + i]], Points[indices[1 + i]], Points[indices[2 + i]]);
        }

        Vector3 VectorForEdge(uint edge) const {
            auto [p0, p1] = VerticesForEdge(edge);
            auto p = p1 - p0;
            p.Normalize();
            return p;
        }

        // Gets the UV vector for an edge
        Vector2 VectorForEdgeUV(uint edge) const {
            auto v = Side->UVs[(edge + 1) % 4] - Side->UVs[edge % 4];
            v.Normalize();
            return v;
        }

        Vector3 GetEdgeMidpoint(uint edge) const {
            auto& p0 = Points[edge];
            auto& p1 = Points[(edge + 1) % 4];
            return (p0 + p1) / 2;
        }

        float Area() const {
            auto v1 = Points[1] - Points[0];
            auto v2 = Points[3] - Points[0];
            return v1.Cross(v2).Length();
        }

        float GetAngleBetween(const Face& face) const {
            auto dir = face.Center() - Center();
            dir.Normalize();
            auto dot = AverageNormal().Dot(dir);
            return acos(dot) * RadToDeg;
        }

        // Check if a face lies directly on top of another face. Ignores vertex order.
        bool Overlaps(const Face& face, float tolerance = 0.01f) const {
            bool overlaps = true;

            for (int i = 0; i < 4; i++) {
                auto& p = Points[i];

                bool pointOverlap = false;
                for (int j = 0; j < 4; j++) {
                    auto& other = face[j];
                    auto vdist = Vector3::Distance(p, other);
                    if (vdist < tolerance)
                        pointOverlap = true;
                }

                overlaps &= pointOverlap; // will evaluate false if no points match
            }

            return overlaps;
        }

        bool SharesIndices(const Face& face) const {
            for (auto& i : face.Indices) {
                for (auto& j : Indices) {
                    if (i == j) return true;
                }
            }

            return false;
        }

        float FlatnessRatio() const {
            auto getRatio = [this](int i0, int i1, int i2, int i3) {
                auto length1 = PointToLineDistance(Points[i0], Points[i1], Points[i2]);
                auto length2 = PointToLineDistance(Points[i0], Points[i1], Points[i3]);
                auto avgLen = (length1 + length2) / 2;
                auto midpoint1 = (Points[i0] + Points[i1]) / 2;
                auto midpoint2 = (Points[i2] + Points[i3]) / 2;
                auto midLen = (midpoint2 - midpoint1).Length();
                return midLen / avgLen;
            };

            auto ratio1 = getRatio(0, 1, 2, 3);
            auto ratio2 = getRatio(1, 2, 3, 0);
            return std::min(ratio1, ratio2);
        }

        void Reflect(span<Vector3> points) const {
            Plane plane(Center(), AverageNormal());
            auto reflect = Matrix::CreateReflection(plane);

            for (auto& point : points)
                point = Vector3::Transform(point, reflect);
        }

        // Insets the vertices towards the center of the face by distance, and offsets along the normal by height.
        Array<Vector3, 4> Inset(float distance, float height) const {
            Array<Vector3, 4> points = Points;
            for (int i = 0; i < points.size(); i++) {
                // Move towards center of face
                auto vectorToCenter = Center() - points[i];
                vectorToCenter.Normalize();
                points[i] += vectorToCenter * distance + AverageNormal() * height;
            }
            return points;
        }

        // Insets each edge using tangent vectors. Maintains an exact distance from each side.
        Array<Vector3, 4> InsetTangent(float distance, float height) const {
            Array<Vector3, 4> points = Points;
            for (int i = 0; i < points.size(); i++) {
                auto tangent = Points[(i + 1) % 4] - Points[i];
                auto bitangent = Points[(i + 3) % 4] - Points[i];
                tangent.Normalize();
                bitangent.Normalize();
                points[i] += tangent * distance + bitangent * distance + AverageNormal() * height;
            }
            return points;
        }

        // Returns the index of the longest edge
        PointID GetLongestEdge() const {
            float indexLen = 0;
            int index = 0;

            for (int i = 0; i < 4; i++) {
                auto len = (Points[(i + 1) % 4] - Points[i]).LengthSquared();
                if (len > indexLen) {
                    index = i;
                    indexLen = len;
                }
            }

            return (PointID)index;
        }

        PointID GetShortestEdge() const {
            float indexLen = FLT_MAX;
            int index = 0;

            for (int i = 0; i < 4; i++) {
                auto len = (Points[(i + 1) % 4] - Points[i]).LengthSquared();
                if (len < indexLen) {
                    index = i;
                    indexLen = len;
                }
            }

            return (PointID)index;
        }

        // Returns true if any points of the face are in front of a plane
        bool InFrontOfPlane(const Plane& plane, float offset = 0) const {
            Vector3 poffset;
            if (offset != 0) poffset += AverageNormal() * offset;
            for (int i = 0; i < 4; i++) {
                auto dist = plane.DotCoordinate(Points[i] + poffset);
                if (dist > 0)
                    return true;
            }

            return false;
        }
    };
}

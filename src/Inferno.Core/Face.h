#pragma once
#include "Level.h"
#include "Utility.h"

namespace Inferno {
    struct FaceHit {
        float Distance;
        Vector3 Normal;
    };

    // Helper to perform operations on a segment face. A face is always 4 points.
    // Do not store long term as it contains references and not a copy.
    template <class TVector = Vector3, class TSide = SegmentSide>
    struct FaceT {
        TVector &P0, &P1, &P2, &P3;
        TSide& Side;
        Array<PointID, 4> Indices;

        FaceT(TVector& p0, TVector& p1, TVector& p2, TVector& p3, TSide& side, const Array<PointID, 4>& indices) :
            P0(p0), P1(p1), P2(p2), P3(p3), Side(side), Indices(indices) {}

        // Conversion to const from non-const
        operator FaceT<const TVector, const TSide>() const {
            return { P0, P1, P2, P3, Side, Indices };
        }

        template <class TLevel = Level>
        static FaceT FromSide(TLevel& level, SegID segId, SideID side) {
            auto& seg = level.GetSegment(segId);
            return FromSide(level, seg, side);
        }

        template <class TLevel = Level>
        static FaceT FromSide(TLevel& level, Tag tag) {
            return FromSide(level, tag.Segment, tag.Side);
        }

        template <class TLevel = Level, class TSeg = Segment>
        static FaceT FromSide(TLevel& level, TSeg& seg, SideID side) {
            auto& sideVerts = SIDE_INDICES[(int)side];
            auto& v0 = level.Vertices[seg.Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[seg.Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[seg.Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[seg.Indices[sideVerts[3]]];
            return FaceT(v0, v1, v2, v3, seg.GetSide(side), seg.GetVertexIndices(side));
        }

        // Returns -1 if no hit, 0 if hit tri 0, 1 if hit tri 1
        // Tolerance grows the triangle
        int Intersects(const Ray& ray, float& dist, bool hitBackface = false, float tolerance = 0.0f) const {
            // bug: if the ray is directly on top of the triangle this returns false
            auto& i = Side.GetRenderIndices();
            bool hitTri0 = hitBackface || Side.Normals[0].Dot(ray.direction) < 0;
            bool hitTri1 = hitBackface || Side.Normals[1].Dot(ray.direction) < 0;

            auto growTriangle = [tolerance](Vector3& p0, Vector3& p1, Vector3& p2, const Vector3& center) {
                auto v0 = GetDirection(p0, center);
                auto v1 = GetDirection(p1, center);
                auto v2 = GetDirection(p2, center);
                p0 += v0 * tolerance;
                p1 += v1 * tolerance;
                p2 += v2 * tolerance;
            };

            {
                auto p0 = GetPoint(i[0]);
                auto p1 = GetPoint(i[1]);
                auto p2 = GetPoint(i[2]);

                if (tolerance != 0) {
                    growTriangle(p0, p1, p2, Side.Centers[0]);
                }

                if (hitTri0 && ray.Intersects(p0, p1, p2, dist))
                    return 0;
            }

            {
                auto p0 = GetPoint(i[3]);
                auto p1 = GetPoint(i[4]);
                auto p2 = GetPoint(i[5]);

                if (tolerance != 0) {
                    growTriangle(p0, p1, p2, Side.Centers[1]);
                }

                if (hitTri1 && ray.Intersects(p0, p1, p2, dist))
                    return 1;
            }

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

        TVector& operator [](int index) const {
            assert(index >= 0);
            switch (index % 4) {
                default:
                case 0: return P0;
                case 1: return P1;
                case 2: return P2;
                case 3: return P3;
            }
        }

        TVector& GetPoint(int index) { return (*this)[index]; }
        const TVector& GetPoint(int index) const { return (*this)[index]; }

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

        Tuple<TVector&, TVector&> VerticesForEdge(uint edge) {
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

        float GetAngleBetween(const FaceT& face) const {
            auto dir = face.Center() - Center();
            dir.Normalize();
            auto dot = AverageNormal().Dot(dir);
            return acos(dot) * RadToDeg;
        }

        // Check if a face lies directly on top of another face. Ignores vertex order.
        bool Overlaps(const FaceT& face, float tolerance = 0.01f) const {
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

        bool SharesIndices(const FaceT& face) const {
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

    using Face = FaceT<>;
    using ConstFace = FaceT<const Vector3, const SegmentSide>;
}

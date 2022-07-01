#pragma once

#pragma once
#include "Utility.h"
#include "Level.h"

namespace Inferno {
    struct FaceHit { float Distance; Vector3 Normal; };

    // Helper to perform operations on a segment face. A face is always 4 points.
    // Do not store long term as it contains references and not a copy.
    struct Face {
        Vector3& P0, & P1, & P2, & P3;
        SegmentSide& Side;
        Array<PointID, 4> Indices;

        Face(Vector3& p0, Vector3& p1, Vector3& p2, Vector3& p3, SegmentSide& side, Array<PointID, 4> indices) :
            P0(p0), P1(p1), P2(p2), P3(p3), Side(side), Indices(indices) {
        }

        static Face FromSide(Level& level, SegID segId, SideID side) {
            auto& seg = level.GetSegment(segId);
            return FromSide(level, seg, side);
        }

        static Face FromSide(Level& level, Tag tag) {
            return FromSide(level, tag.Segment, tag.Side);
        }

        static Face FromSide(Level& level, Segment& seg, SideID side) {
            auto& sideVerts = SideIndices[(int)side];
            auto& v0 = level.Vertices[seg.Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[seg.Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[seg.Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[seg.Indices[sideVerts[3]]];
            return Face(v0, v1, v2, v3, seg.GetSide(side), seg.GetVertexIndices(side));
        }

        bool Intersects(const Ray& ray, float& dist) const {
            auto indices = Side.GetRenderIndices();
            return
                ray.Intersects(GetPoint(indices[0]), GetPoint(indices[1]), GetPoint(indices[2]), dist) ||
                ray.Intersects(GetPoint(indices[3]), GetPoint(indices[4]), GetPoint(indices[5]), dist);
        }

        Array<Vector3, 4> CopyPoints() const {
            return { P0, P1, P2, P3 };
        }

        Vector3& operator [](int index) const {
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
        int16 GetClosestEdge(const Vector3& pos) {
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

        //Vector3 Tangent() const {
        //    auto tangent = Vector3(P2.x, P2.y, P2.z) - Vector3(P1.x, P1.y, P1.z);
        //    tangent.Normalize();
        //    return tangent;
        //}

        Vector3 Center() const {
            return Side.Center;
        }

        Tuple<Vector3&, Vector3&> VerticesForEdge(uint edge) {
            auto& p0 = GetPoint(edge);
            auto& p1 = GetPoint(edge + 1);
            return { p0, p1 };
        }

        // Makes a copy of verts!
        Array<Vector3, 3> VerticesForPoly0() const {
            auto indices = Side.GetRenderIndices();
            return { GetPoint(indices[0]), GetPoint(indices[1]), GetPoint(indices[2]) };
        }

        // Makes a copy of verts!
        Array<Vector3, 3> VerticesForPoly1() const {
            auto indices = Side.GetRenderIndices();
            return { GetPoint(indices[3]), GetPoint(indices[4]), GetPoint(indices[5]) };
        }


        Plane GetPlane0() {
            auto indices = Side.GetRenderIndices();
            return Plane(GetPoint(indices[0]), GetPoint(indices[1]), GetPoint(indices[2]));
        }

        Plane GetPlane1() {
            auto indices = Side.GetRenderIndices();
            return Plane(GetPoint(indices[3]), GetPoint(indices[4]), GetPoint(indices[5]));
        }

        Vector3 VectorForEdge(uint edge) {
            auto [p0, p1] = VerticesForEdge(edge);
            auto p = p1 - p0;
            p.Normalize();
            return p;
        }

        // Gets the texture space vector for an edge
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

        // Returns the distance and normal that the object would intersect in the next update tick
        Option<FaceHit> Intersects(const Object& obj) {
            auto indices = Side.GetRenderIndices();

            auto vec = obj.Transform.Translation() - obj.PrevTransform.Translation();
            auto dist = vec.Length();
            if (std::abs(dist) <= 0.001f) return {};
            vec.Normalize();

            Ray ray(obj.PrevTransform.Translation(), vec);
            float intersect{};
            if (ray.Intersects(GetPoint(indices[0]), GetPoint(indices[1]), GetPoint(indices[2]), intersect)) {
                if (intersect < (dist + obj.Radius))
                    return { { intersect, Side.Normals[0] } };
            }

            if (ray.Intersects(GetPoint(indices[3]), GetPoint(indices[4]), GetPoint(indices[5]), intersect)) {
                if (intersect < (dist + obj.Radius))
                    return { { intersect, Side.Normals[1] } };
            }

            //if (ray.Intersects(GetPoint(indices[0]), GetPoint(indices[1]), GetPoint(indices[2]), intersect) ||
            //    ray.Intersects(GetPoint(indices[3]), GetPoint(indices[4]), GetPoint(indices[5]), intersect)) {
            //    if (intersect < (dist + obj.Radius))
            //        return intersect;
            //}

            return {};
        }
    };
}
#include "pch.h"
#include "Segment.h"
#include "Level.h"
#include "Face.h"

namespace Inferno {
    bool Segment::SideIsSolid(SideID side, Level& level) const {
        if (SideHasConnection(side)) {
            if (auto wall = level.TryGetWall(Sides[(int)side].Wall))
                return wall->IsSolid(); // walls might be solid

            return false; // open side with no wall
        }

        return true; // no connection or wall
    }

    Vector3 CreateNormal(const Vector3& v0, const Vector3& v1, const Vector3& v2) {
        auto normal = (v1 - v0).Cross(v2 - v1);
        normal.Normalize();
        if (!IsNormalized(normal)) return Vector3::UnitY; // return a dummy normal to prevent errors
        return normal;
    }

    Vector3 CreateTangent(const Vector3& v0, const Vector3& v1) {
        auto delta = v1 - v0;
        delta.Normalize();
        return delta;
    }

    void Segment::UpdateGeometricProps(const Level& level) {
        for (auto& s : SideIDs) {
            auto& side = GetSide(s);
            auto& sideVerts = SideIndices[(int)s];
            auto& v0 = level.Vertices[Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[Indices[sideVerts[3]]];
            
            // Always split sides to be convex
            auto n0 = CreateNormal(v0, v1, v2);
            auto dot = n0.Dot(v3 - v1);
            side.Type = dot >= 0 ? SideSplitType::Tri02 : SideSplitType::Tri13;

            if (side.Type == SideSplitType::Tri02) {
                side.Normals[0] = CreateNormal(v0, v1, v2); // 0-2 split
                side.Normals[1] = CreateNormal(v0, v2, v3); // 0-2 split
                side.Tangents[0] = CreateTangent(v0, v1);
                side.Tangents[1] = CreateTangent(v2, v3);
                side.Centers[0] = (v0 + v1 + v2) / 3;
                side.Centers[1] = (v0 + v2 + v3) / 3;
            }
            else {
                side.Normals[0] = CreateNormal(v0, v1, v3); // 1-3 split
                side.Normals[1] = CreateNormal(v1, v2, v3); // 1-3 split
                side.Tangents[0] = CreateTangent(v0, v1);
                side.Tangents[1] = CreateTangent(v2, v3);
                side.Centers[0] = (v0 + v1 + v3) / 3;
                side.Centers[1] = (v1 + v2 + v3) / 3;
            }

            auto dist = abs(PointToPlaneDistance(v3, v0, n0));
            if (dist <= FixToFloat(250)) {
                side.Type = SideSplitType::Quad;
            }

            side.AverageNormal = (side.Normals[0] + side.Normals[1]) / 2.0f;
            side.AverageNormal.Normalize();
            side.Center = (v0 + v1 + v2 + v3) / 4;
        }

        auto verts = GetVertices(level);

        Vector3 center;
        for (auto v : verts)
            center += *v;

        Center = center / (float)verts.size();
    }

    float Segment::GetEstimatedVolume(Level& level) {
        auto front = Face::FromSide(level, *this, SideID::Front);
        auto bottom = Face::FromSide(level, *this, SideID::Bottom);
        return front.Area() * bottom.Area();
    }

    bool Segment::IsZeroVolume(Level& level) {
        auto front = Face::FromSide(level, *this, SideID::Front);
        auto back = Face::FromSide(level, *this, SideID::Back);
        if (front.Distance(back.Center()) <= 0.1f) return true;

        auto bottom = Face::FromSide(level, *this, SideID::Bottom);
        auto top = Face::FromSide(level, *this, SideID::Top);
        if (bottom.Distance(top.Center()) <= 0.1f) return true;

        auto right = Face::FromSide(level, *this, SideID::Right);
        auto left = Face::FromSide(level, *this, SideID::Left);
        if (right.Distance(left.Center()) <= 0.1f) return true;

        return false;
    }

    Array<const Vector3*, 8> Segment::GetVertices(const Level& level) const {
        auto front = GetVertexIndices(SideID::Front);
        auto back = GetVertexIndices(SideID::Back);

        Array<const Vector3*, 8> verts{};
        for (int i = 0; i < 4; i++) {
            verts[i] = &level.Vertices[front[i]];
            verts[i + 4] = &level.Vertices[back[i]];
        }

        return verts;
    }

    Array<Vector3, 4> Segment::GetVertices(const Level&, SideID) const {
        return Array<Vector3, 4>();
    }

    Array<Vector3, 8> Segment::CopyVertices(const Level& level) {
        auto front = GetVertexIndices(SideID::Front);
        auto back = GetVertexIndices(SideID::Back);

        Array<Vector3, 8> verts{};
        for (int i = 0; i < 4; i++) {
            verts[i] = level.Vertices[front[i]];
            verts[i + 4] = level.Vertices[back[i]];
        }

        return verts;
    }
}

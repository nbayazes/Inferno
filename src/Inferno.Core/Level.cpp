#include "pch.h"
#include "Level.h"
#include "Streams.h"
#include "Face.h"

namespace Inferno {
    List<SegID> Level::SegmentsByVertex(uint i) {
        List<SegID> segments;
        auto id = SegID(0);

        for (auto& seg : Segments) {
            for (auto& v : seg.Indices)
                if (v == i) segments.push_back(id);

            id = SegID((int)id + 1);
        }

        return segments;
    }

    Vector3 CreateNormal(const Vector3& v0, const Vector3& v1, const Vector3& v2) {
        auto normal = (v1 - v0).Cross(v2 - v1);
        normal.Normalize();
        return normal;
    }

    void Segment::UpdateNormals(Level& level) {
        for (auto& s : SideIDs) {
            auto& side = GetSide(s);
            auto face = Face::FromSide(level, *this, s);

            // Always split sides to be convex
            auto n0 = CreateNormal(face[0], face[1], face[2]);
            auto dot = n0.Dot(face[3] - face[1]);
            side.Type = dot >= 0 ? SideSplitType::Tri02 : SideSplitType::Tri13;

            if (side.Type == SideSplitType::Tri02) {
                side.Normals[0] = CreateNormal(face[0], face[1], face[2]); // 0-2
                side.Normals[1] = CreateNormal(face[0], face[2], face[3]); // 0-2
                side.Centers[0] = (face[0] + face[1] + face[2]) / 3;
                side.Centers[1] = (face[0] + face[2] + face[3]) / 3;
            }
            else {
                side.Normals[0] = CreateNormal(face[0], face[1], face[3]); // 1-3
                side.Normals[1] = CreateNormal(face[1], face[2], face[3]); // 1-3
                side.Centers[0] = (face[0] + face[1] + face[3]) / 3;
                side.Centers[1] = (face[1] + face[2] + face[3]) / 3;
            }

            auto dist = abs(PointToPlaneDistance(face[3], face[0], n0));
            if (dist <= FixToFloat(250)) {
                side.Type = SideSplitType::Quad;
            }

            side.AverageNormal = (side.Normals[0] + side.Normals[1]) / 2.0f;
            side.AverageNormal.Normalize();
            side.Center = (face[0] + face[1] + face[2] + face[3]) / 4;
        }
    }

    void Segment::UpdateCenter(const Level& level) {
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

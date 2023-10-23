#include "pch.h"
#include "Segment.h"

#include <spdlog/spdlog.h>

#include "Level.h"
#include "Face.h"

namespace Inferno {
    void Segment::RemoveObject(ObjID id) {
        //assert(Seq::contains(Objects, id));
        Seq::remove(Objects, id);
    }

    void Segment::AddObject(ObjID id) {
        //assert(!Seq::contains(Objects, id));
        if (Seq::contains(Objects, id)) {
            SPDLOG_WARN("Segment already contains object id {}", id);
            return;
        }
        Objects.push_back(id);
    }

    bool Segment::SideIsSolid(SideID side, const Level& level) const {
        if (SideHasConnection(side)) {
            if (auto wall = level.TryGetWall(Sides[(int)side].Wall))
                return wall->IsSolid(); // walls might be solid

            return false; // open side with no wall
        }

        return true; // no connection or wall
    }

    Vector3 CreateTangent(const Vector3& v0, const Vector3& v1) {
        auto delta = v1 - v0;
        delta.Normalize();
        return delta;
    }

    //  Returns the lowest 3 indices of a side, used for stable comparison of joined sides
    Array<int, 3> GetLowestIndices(SideID side, const Array<PointID, MAX_VERTICES>& segIndices) {
        auto& sideVerts = SIDE_INDICES[(int)side];

        Array<int, 4> indices = {
            segIndices[sideVerts[0]],
            segIndices[sideVerts[1]],
            segIndices[sideVerts[2]],
            segIndices[sideVerts[3]]
        };

        ranges::sort(indices);
        return { indices[0], indices[1], indices[2] };
    }

    void Segment::UpdateGeometricProps(const Level& level) {
        for (auto& sideId : SideIDs) {
            auto& side = GetSide(sideId);
            auto& sideVerts = SIDE_INDICES[(int)sideId];
            auto& v0 = level.Vertices[Indices[sideVerts[0]]];
            auto& v1 = level.Vertices[Indices[sideVerts[1]]];
            auto& v2 = level.Vertices[Indices[sideVerts[2]]];
            auto& v3 = level.Vertices[Indices[sideVerts[3]]];

            auto n0 = CreateNormal(v0, v1, v2);

            if (SideHasConnection(sideId)) {
                // Use the same triangle to compare both open sides so they join consistently
                auto indices = GetLowestIndices(sideId, Indices);
                auto normal = CreateNormal(level.Vertices[indices[0]], level.Vertices[indices[1]], level.Vertices[indices[2]]);
                auto dotNormal = n0.Dot(normal); // Check if ref triangle is flipped
                side.Type = dotNormal >= 0 ? SideSplitType::Tri02 : SideSplitType::Tri13;
            }
            else {
                // Always split solid sides to be convex
                auto dot = n0.Dot(v3 - v1);
                side.Type = dot >= 0 ? SideSplitType::Tri02 : SideSplitType::Tri13;
            }

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
            if (dist <= PLANAR_TOLERANCE) {
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

    float Segment::GetEstimatedVolume() const {
        auto d0 = Vector3::Distance(Sides[0].Center, Sides[2].Center);
        auto d1 = Vector3::Distance(Sides[1].Center, Sides[3].Center);
        auto d2 = Vector3::Distance(Sides[4].Center, Sides[5].Center);
        return d0 * d1 * d2;
        //auto front = Face::FromSide(level, *this, SideID::Front);
        //auto bottom = Face::FromSide(level, *this, SideID::Bottom);
        //return front.Area() * bottom.Area();
    }

    float Segment::GetLongestEdge() const {
        auto d0 = Vector3::Distance(Sides[0].Center, Sides[2].Center);
        auto d1 = Vector3::Distance(Sides[1].Center, Sides[3].Center);
        auto d2 = Vector3::Distance(Sides[4].Center, Sides[5].Center);
        return std::max(d0, std::max(d1, d2));
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

    Array<Vector3, 8> Segment::CopyVertices(const Level& level) const {
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

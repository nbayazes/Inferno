#include "pch.h"
#include "Icosphere.h"
#include "Graphics/Render.h"
#include "Types.h"
#include "unordered_dense.h"
#include "VertexTypes.h"

namespace Inferno {
    Render::ModelMesh CreateIcosphere(float radius, uint subdivisions) {
        // https://schneide.blog/2016/07/15/generating-an-icosphere-in-c/
        // https://www.alexisgiard.com/icosahedron-sphere/
        constexpr float X = 0.525731112119133606f;
        constexpr float Z = 0.850650808352039932f;
        constexpr float N = 0.0f;

        List<ObjectVertex> vertices = {
            { { -X, N, Z } }, { { X, N, Z } }, { { -X, N, -Z } }, { { X, N, -Z } },
            { { N, Z, X } }, { { N, Z, -X } }, { { N, -Z, X } }, { { N, -Z, -X } },
            { { Z, X, N } }, { { -Z, X, N } }, { { Z, -X, N } }, { { -Z, -X, N } }
        };

        for (auto& v : vertices) {
            v.Position.Normalize();
            v.Normal = v.Position;
        }

        List<uint16> indices = {
            0, 4, 1, 0, 9, 4, 9, 5, 4, 4, 5, 8, 4, 8, 1,
            8, 10, 1, 8, 3, 10, 5, 3, 8, 5, 2, 3, 2, 7, 3,
            7, 10, 3, 7, 6, 10, 7, 11, 6, 11, 0, 6, 0, 1, 6,
            6, 1, 10, 9, 0, 11, 9, 11, 2, 9, 2, 5, 7, 2, 11,
        };


        using Lookup = ankerl::unordered_dense::map<uint64, uint32>;
        auto vertexForEdge = [&](Lookup& lookup, std::vector<ObjectVertex>& verts, uint32 first, uint32 second) {
            uint64 key = first < second
                ? uint64(first) << 32 | second
                : uint64(second) << 32 | first;

            auto inserted = lookup.insert({ key, uint32(verts.size()) });
            if (inserted.second) {
                auto& edge0 = verts[first].Position;
                auto& edge1 = verts[second].Position;
                auto point = edge0 + edge1;
                point.Normalize();
                verts.push_back(ObjectVertex{ .Position = point, .Normal = point });
            }

            return inserted.first->second;
        };

        for (uint i = 0; i < subdivisions; ++i) {
            Lookup lookup;
            std::vector<uint16> result;

            for (uint16 j = 0; j < indices.size(); j += 3) {
                std::array vi = { indices[j + 0], indices[j + 1], indices[j + 2] };

                std::array<uint16, 3> mid{};
                for (uint16 edge = 0; edge < 3; ++edge) {
                    mid[edge] = (uint16)vertexForEdge(lookup, vertices, vi[edge], vi[(edge + 1) % 3]);
                }

                result.push_back(vi[0]);
                result.push_back(mid[0]);
                result.push_back(mid[2]);

                result.push_back(vi[1]);
                result.push_back(mid[1]);
                result.push_back(mid[0]);

                result.push_back(vi[2]);
                result.push_back(mid[2]);
                result.push_back(mid[1]);

                result.push_back(mid[0]);
                result.push_back(mid[1]);
                result.push_back(mid[2]);
            }

            indices = std::move(result);
            //NOVA_LOG("Subdivision level: {}, triangles = {}", i + 1, indices.size() / 3);
        }

        // UV map the sphere
        // https://observablehq.com/@mourner/uv-mapping-an-icosphere
        for (auto& v : vertices) {
            v.UV = Vector2(
                (std::atan2(v.Position.z, v.Position.x) / (2.f * DirectX::XM_PI)) + 0.5f,
                (std::asin(v.Position.y) / DirectX::XM_PI)) + Vector2(0.5f, 0.5f);
        }

        //auto scaleMatrix = Matrix::CreateScale(radius);
        ////auto scaleMatrix = Matrix::CreateScale(radius, radius * 2, radius * 1.5f);
        //for (auto& v : vertices) {
        //    v.Position = Vector3::Transform(v.Position, scaleMatrix);
        //}

        //for (auto& v : vertices) {
        //    v.Position = Vector3::Transform(v.Position, scaleMatrix);
        //    auto s = 50.0f; // scale
        //    Vector3 strength = { 1.0f, 1.0f, 1.0f };
        //    strength *= 15;
        //    auto seed = 0;

        //    auto& p = v.Position;
        //    //p += v.Normal * OpenSimplex2::Noise3(seed, p.x * s, p.y * s, p.z * s) * 20;
        //    auto ps = p / s;
        //    auto x = OpenSimplex2::Noise3(seed, 0, ps.y, ps.z) * strength.x;
        //    auto y = OpenSimplex2::Noise3(seed, ps.x, 0, ps.z) * strength.y;
        //    auto z = OpenSimplex2::Noise3(seed, ps.x, ps.y, 0) * strength.z;
        //    p += Vector3{ x, y, z };

        //    //s = 1.0f;
        //    //v.Position += v.Normal * OpenSimplex2::Noise3(0, v.Position.x * s, v.Position.y * s, v.Position.z * s) * 5;
        //    v.Color = Color(1, 1, 1);
        //}

        ASSERT(vertices.size() < USHORT_MAX);
        return { vertices, indices };
    }
}

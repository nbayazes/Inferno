#pragma once

#include "Buffers.h"
#include "EffectClip.h"
#include "logging.h"
#include "Polymodel.h"

namespace Inferno::Render {
    struct Mesh {
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        uint IndexCount;
        TexID Texture;
        EClipID EffectClip = EClipID::None;
    };

    class MeshBuffer {
        List<Mesh> _meshes; // a single model can have multiple meshes
        PackedBuffer _buffer{ 1024 * 1024 * 10 };

        struct MeshIndex {
            // Each mesh submodel can have multiple meshes due to textures
            Array<Array<Mesh*, 20>, MAX_SUBMODELS> Meshes;
            bool Loaded = false;

            MeshIndex() {
                for (auto& item : Meshes)
                    std::fill(item.begin(), item.end(), nullptr);
            }
        };

        List<MeshIndex> _handles;

    public:

        MeshBuffer(size_t capacity) {
            SPDLOG_INFO("Created mesh buffer with capacity {}", capacity);
            constexpr int AVG_TEXTURES_PER_MESH = 3;
            auto size = MAX_SUBMODELS * capacity * AVG_TEXTURES_PER_MESH;
            _meshes.reserve(size);
            _handles.resize(capacity);
        }

        void LoadModel(ModelID id) {
            if ((int)id >= _handles.size()) return;
            auto& handle = _handles[(int)id];
            if (handle.Loaded) return;

            //SPDLOG_INFO("Loading model {}", id);
            auto& model = Resources::GetModel(id);

            for (int smIndex = 0; auto & submodel : model.Submodels) {
                int vertexCount = (int)submodel.ExpandedPoints.size();
                assert(vertexCount % 3 == 0);
                List<ObjectVertex> verts;
                verts.reserve(vertexCount);

                // load vertex buffer
                for (int i = 0; i < vertexCount; i++) {
                    // combine points and uvs into vertices
                    auto& uv = i >= submodel.UVs.size() ? Vector3() : submodel.UVs[i];
                    verts.emplace_back(ObjectVertex{ submodel.ExpandedPoints[i], Vector2{ uv.x, uv.y }, submodel.ExpandedColors[i] });
                }

                // calculate normals
                for (int i = 0; i < vertexCount; i += 3) {
                    // create vectors for two edges and cross to get normal
                    auto v1 = verts[i + 1].Position - verts[i].Position;
                    auto v2 = verts[i + 2].Position - verts[i].Position;
                    auto normal = v1.Cross(v2);
                    normal.Normalize();
                    verts[i].Normal = verts[i + 1].Normal = verts[i + 2].Normal = normal;
                }

                auto vertexView = _buffer.PackVertices(verts);

                // Create meshes
                for (int16 slot = 0; auto & indices : submodel.ExpandedIndices) {
                    if (indices.size() != 0) { // don't upload empty indices
                        auto& mesh = _meshes.emplace_back();
                        handle.Meshes[smIndex][slot] = &mesh;
                        mesh.VertexBuffer = vertexView;
                        mesh.IndexBuffer = _buffer.PackIndices(indices);
                        mesh.IndexCount = (uint)indices.size();
                        mesh.Texture = Resources::LookupModelTexID(model, slot);
                        mesh.EffectClip = Resources::GetEffectClip(mesh.Texture);
                    }
                    slot++;
                }

                smIndex++;
            }

            handle.Loaded = true;
        }

        MeshIndex& GetHandle(ModelID id) {
            return _handles[(int)id];
        }

        Mesh& GetMesh(uint16 index) {
            return _meshes[index];
        }

        const auto Meshes() { return _meshes; }
    };
}
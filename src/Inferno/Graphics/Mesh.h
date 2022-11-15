#pragma once

#include "Buffers.h"
#include "EffectClip.h"
#include "logging.h"
#include "Polymodel.h"
#include "ShaderLibrary.h"

namespace Inferno::Render {
    // An object mesh used for rendering
    struct Mesh {
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        uint IndexCount;
        TexID Texture;
        EClipID EffectClip = EClipID::None;
        bool HasTransparentTexture = false;
    };

    // Pointers to individual meshes in a polymodel
    struct MeshIndex {
        // A lookup of meshes based on submodel and then texture
        Dictionary<int, Dictionary<int, Mesh*>> Meshes;
        bool Loaded = false;
        bool HasTransparentTexture = false;
    };

    class MeshBuffer {
        List<Mesh> _meshes; // Buffer stores multiple meshes
        PackedBuffer _buffer{ 1024 * 1024 * 10 };
        List<MeshIndex> _handles;
        size_t _capacity, _capacityD3;
    public:
        MeshBuffer(size_t capacity, size_t capacityD3)
            : _capacity(capacity), _capacityD3(capacityD3) {
            SPDLOG_INFO("Created mesh buffer with capacity {}", capacity);
            constexpr int AVG_TEXTURES_PER_MESH = 3;
            auto size = MAX_SUBMODELS * (capacity + capacityD3) * AVG_TEXTURES_PER_MESH;
            _meshes.reserve(size);
            _handles.resize(capacity + capacityD3);
        }

        // Loads a D1/D2 model
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
                        auto& ti = Resources::GetTextureInfo(mesh.Texture);
                        if (ti.Transparent) {
                            mesh.HasTransparentTexture = true;
                            handle.HasTransparentTexture = true;
                        }
                    }
                    slot++;
                }

                smIndex++;
            }

            handle.Loaded = true;
        }

        // Loads a D3 model
        void LoadOutrageModel(const Outrage::Model& model, ModelID id) {
            auto& handle = _handles[_capacity + (int)id];
            if (handle.Loaded) return;

            for (int smIndex = 0; auto & submodel : model.Submodels) {
                struct SubmodelMesh {
                    List<ObjectVertex> Vertices;
                    List<uint16> Indices;
                    int16 Index = 0;
                };

                Dictionary<int, SubmodelMesh> smMeshes;

                // combine uvs from faces with the vertices
                for (auto& face : submodel.Faces) {
                    Color color = face.Color;

                    const auto& fv0 = face.Vertices[0];
                    const auto& v0 = submodel.Vertices[fv0.Index];

                    auto fvx = &face.Vertices[1];
                    auto vx = &submodel.Vertices[fvx->Index];

                    // convert triangle fans to triangle lists
                    for (int i = 2; i < face.Vertices.size(); i++) {
                        const auto& fv = face.Vertices[i];
                        const auto& v = submodel.Vertices[fv.Index];

                        auto AddVert = [&](const Outrage::Submodel::Vertex& vtx, const Vector2& uv) {
                            color.A(vtx.Alpha);
                            auto& smm = smMeshes[face.TexNum];
                            smm.Vertices.push_back(ObjectVertex{
                                .Position = vtx.Position,
                                .UV = uv,
                                .Color = color,
                                .Normal = vtx.Normal
                                                   });
                            smm.Indices.push_back(smm.Index++);
                        };

                        AddVert(v0, fv0.UV);
                        AddVert(*vx, fvx->UV);
                        AddVert(v, fv.UV);

                        fvx = &fv;
                        vx = &v;
                    }
                }

                for (auto& [i, smm] : smMeshes) {
                    auto& mesh = _meshes.emplace_back();
                    handle.Meshes[smIndex][i] = &mesh;
                    mesh.VertexBuffer = _buffer.PackVertices(smm.Vertices);
                    mesh.IndexBuffer = _buffer.PackIndices(smm.Indices);
                    mesh.IndexCount = (uint)smm.Indices.size();
                }

                smIndex++;
            }

            handle.Loaded = true;
        }

        MeshIndex& GetHandle(ModelID id) {
            return _handles[(int)id];
        }

        MeshIndex& GetOutrageHandle(ModelID id) {
            return _handles[_capacity + (int)id];
        }
    };
}
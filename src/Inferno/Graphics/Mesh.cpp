#include "pch.h"
#include "Mesh.h"

#include "MaterialLibrary.h"

namespace Inferno::Render {
    void GetTangentBitangent(span<ObjectVertex> verts) {
        auto edge1 = verts[1].Position - verts[0].Position;
        auto edge2 = verts[2].Position - verts[0].Position;
        auto deltaUV1 = verts[1].UV - verts[0].UV;
        auto deltaUV2 = verts[2].UV - verts[0].UV;

        static_assert(std::numeric_limits<float>::is_iec559); // Check that nan / inf behavior is defined
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        if (std::isnan(f) || std::isinf(f)) {
            // Invalid UVs or untextured side
            edge1.Normalize(verts[0].Tangent);
            verts[1].Tangent = verts[2].Tangent = verts[0].Tangent;
            auto bitangent = verts[0].Tangent.Cross(verts[0].Normal);
            verts[0].Bitangent = verts[1].Bitangent = verts[2].Bitangent = bitangent;
        }
        else {
            Vector3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * f;
            tangent.Normalize();

            Vector3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * f;
            bitangent.Normalize();

            verts[0].Tangent = verts[1].Tangent = verts[2].Tangent = tangent;
            verts[0].Bitangent = verts[1].Bitangent = verts[2].Bitangent = bitangent;
        }
    }

    void MeshBuffer::LoadModel(ModelID id) {
        if ((int)id >= _handles.size()) return;
        auto& handle = _handles[(int)id];
        if (handle.Loaded) return;

        //SPDLOG_INFO("Loading model {}", id);
        auto& model = Resources::GetModel(id);

        for (int smIndex = 0; auto& submodel : model.Submodels) {
            auto vertexCount = (int)submodel.ExpandedPoints.size();
            assert(vertexCount % 3 == 0);
            List<ObjectVertex> verts;
            verts.reserve(vertexCount);

            auto texId = TexID::None; // estimated material for this mesh
            bool isTransparent = false;

            // load vertex buffer
            for (int i = 0; i < vertexCount; i++) {
                // combine points and uvs into vertices
                auto& uv = i >= submodel.UVs.size() ? Vector2() : submodel.UVs[i];
                auto& p = submodel.ExpandedPoints[i];
                ObjectVertex v{ p.Point, uv, submodel.ExpandedColors[i] };
                if (p.TexSlot == -1) {
                    v.TexID = (int)WHITE_MATERIAL;
                }
                else {
                    texId = Resources::LookupModelTexID(model, p.TexSlot);
                    isTransparent |= Resources::GetTextureInfo(texId).Transparent;
                    auto vclip = Resources::GetEffectClipID(texId);
                    v.TexID = vclip > EClipID::None ? VCLIP_RANGE + (int)vclip : (int)texId;
                }
                verts.push_back(v);
            }

            // calculate normals
            for (int i = 0; i < vertexCount; i += 3) {
                // create vectors for two edges and cross to get normal
                auto v1 = verts[i + 1].Position - verts[i].Position;
                auto v2 = verts[i + 2].Position - verts[i].Position;
                auto normal = -v1.Cross(v2);
                normal.Normalize();
                verts[i].Normal = verts[i + 1].Normal = verts[i + 2].Normal = normal;

                GetTangentBitangent(std::span{ &verts[i], 3 });
            }

            auto vertexView = _buffer.PackVertices(verts);

            // Create meshes
            for (int16 slot = 0; auto& indices : submodel.ExpandedIndices) {
                if (indices.size() == 0) continue; // don't upload empty indices

                auto& mesh = _meshes.emplace_back();
                handle.Meshes[smIndex][slot] = &mesh;
                mesh.VertexBuffer = vertexView;
                mesh.IndexBuffer = _buffer.PackIndices(indices);
                mesh.IndexCount = (uint)indices.size();
                mesh.Texture = texId == TexID::None ? WHITE_MATERIAL : texId; // for flat shaded meshes
                //if (mesh.Texture == TexID::None) mesh.Texture = WHITE_MATERIAL; 
                mesh.EffectClip = Resources::GetEffectClipID(mesh.Texture);
                mesh.IsTransparent = isTransparent;
                handle.IsTransparent = isTransparent;
                slot++;
            }

            smIndex++;
        }

        handle.Loaded = true;
    }

    void MeshBuffer::LoadOutrageModel(const Outrage::Model& model, ModelID id) {
        auto& handle = _handles[_capacity + (int)id];
        if (handle.Loaded) return;

        Render::Materials->LoadTextures(model.Textures);

        for (int smIndex = 0; auto& submodel : model.Submodels) {
            struct SubmodelMesh {
                List<ObjectVertex> Vertices;
                List<uint16> Indices;
                int16 Index = 0;
            };

            Dictionary<int, SubmodelMesh> smMeshes;
            auto tid = TexID::None; // purposely snapshot last face's texture
            // todo: split meshes based on transparency - were the original models designed with this in mind?
            
            // combine uvs from faces with the vertices
            for (auto& face : submodel.Faces) {
                if (face.TexNum == -1) continue; // Skip untextured faces as they are metadata
                tid = Render::Materials->Find(model.Textures[face.TexNum]);
                Color color = face.Color;

                const auto& fv0 = face.Vertices[0];
                const auto& v0 = submodel.Vertices[fv0.Index];

                auto fvx = &face.Vertices[1];
                auto vx = &submodel.Vertices[fvx->Index];

                // convert triangle fans to triangle lists
                for (int i = 2; i < face.Vertices.size(); i++) {
                    const auto& fv = face.Vertices[i];
                    const auto& v = submodel.Vertices[fv.Index];
                    auto startSize = smMeshes[0].Vertices.size();

                    auto addVert = [&](const Outrage::Submodel::Vertex& vtx, const Vector2& uv) {
                        color.A(vtx.Alpha);
                        //auto& smm = smMeshes[face.TexNum];
                        auto& smm = smMeshes[0]; // bindless

                        smm.Vertices.push_back(ObjectVertex{
                            .Position = vtx.Position,
                            .UV = uv,
                            .Color = color,
                            .Normal = vtx.Normal,
                            .TexID = (int)tid
                        });
                        smm.Indices.push_back(smm.Index++);
                    };

                    addVert(v0, fv0.UV);
                    addVert(*vx, fvx->UV);
                    addVert(v, fv.UV);

                    GetTangentBitangent(std::span{ &smMeshes[0].Vertices[startSize], 3 });

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
                mesh.Texture = tid;
            }

            smIndex++;
        }

        handle.Loaded = true;
    }
}

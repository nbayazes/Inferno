#include "pch.h"
#include "LevelMesh.h"

#include "MaterialLibrary.h"
#include "Render.h"
#include "Resources.h"

namespace Inferno {
    using namespace DirectX;

    constexpr bool TMapIsLava(LevelTexID id) {
        constexpr std::array tids = { 291, 378, 404, 405, 406, 407, 408, 409 };
        return Seq::contains(tids, (int)id);
    }

    bool SegHasLava(const Segment& seg) {
        for (auto& side : seg.Sides) {
            if (TMapIsLava(side.TMap)) return true;
        }
        return false;
    }

    HeatVolume CreateHeatVolumes(Level& level) {
        // Discover all verts with lava on them
        Set<uint16> heatIndices;
        for (auto& seg : level.Segments) {
            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);
                if (!TMapIsLava(side.TMap)) continue;
                auto indicesForSide = seg.GetVertexIndices(sideId);
                for (int16 i : indicesForSide)
                    heatIndices.insert(i);
            }
        }

        // Discover all segments that touch
        Set<SegID> heatSegs;
        for (int sid = -1; auto& seg : level.Segments) {
            sid++;
            for (auto i : seg.Indices)
                if (Seq::contains(heatIndices, i))
                    heatSegs.insert(SegID(sid));
        }

        // Create volumes from segments containing lava verts
        List<uint16> indices;
        List<FlatVertex> vertices;

        for (auto& segId : heatSegs) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                // cull faces that connect to another segment containing lava. UNLESS has a wall
                // to do this properly, external facing should be culled on lava falls, otherwise Z fighting will occur
                // ALSO: only closed walls / doors should count (not triggers)
                if (Seq::contains(heatSegs, segId) &&
                    !level.TryGetWall({ (SegID)segId, sideId }))
                    continue;

                Array<FlatVertex, 4> sideVerts;

                bool isLit = false;

                for (int i = 0; auto& v : seg.GetVertexIndices(sideId)) {
                    sideVerts[i].Position = level.Vertices[v];
                    if (Seq::contains(heatIndices, v)) {
                        sideVerts[i].Color = Color{ 1, 1, 1, 1 };
                        isLit = true;
                    }
                    else {
                        sideVerts[i].Color = Color{ 1, 1, 1, 0 };
                    }
                    i++;
                }

                if (!isLit) continue;

                auto indexOffset = (uint16)vertices.size();
                indices.push_back(indexOffset + 0);
                indices.push_back(indexOffset + 1);
                indices.push_back(indexOffset + 2);

                // Triangle 2
                indices.push_back(indexOffset + 0);
                indices.push_back(indexOffset + 2);
                indices.push_back(indexOffset + 3);
                for (auto& v : sideVerts)
                    vertices.push_back(v);
            }
        }

        return { indices, vertices };
    }

    Vector2 ApplyOverlayRotation(const SegmentSide& side, Vector2 uv) {
        float overlayAngle = GetOverlayRotationAngle(side.OverlayRotation);
        return Vector2::Transform(uv, Matrix::CreateRotationZ(overlayAngle));
    }

    Tuple<Vector3, Vector3> GetTangentBitangent(const Array<Vector3, 4>& verts,
                                                const Array<Vector2, 4>& uvs,
                                                const Array<uint16, 6>& indices,
                                                int tri) {
        auto j = tri == 1 ? 3 : 0;
        auto edge1 = verts[indices[1 + j]] - verts[indices[j]];
        auto edge2 = verts[indices[2 + j]] - verts[indices[j]];
        auto deltaUV1 = uvs[indices[1 + j]] - uvs[indices[j]];
        auto deltaUV2 = uvs[indices[2 + j]] - uvs[indices[j]];

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        Vector3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * f;
        tangent.Normalize();

        Vector3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * f;
        bitangent.Normalize();

        return { tangent, bitangent };
    }

    void AddPolygon(const Array<Vector3, 4>& verts,
                    const Array<Vector2, 4>& uvs,
                    const Array<Color, 4>& colors,
                    LevelGeometry& geo,
                    LevelChunk& chunk,
                    const SegmentSide& side,
                    TexID tex1, 
                    TexID tex2) {
        auto startIndex = geo.Vertices.size();
        chunk.AddQuad((uint16)startIndex);

        auto& indices = side.GetRenderIndices();
        auto [tangent1, bitangent1] = GetTangentBitangent(verts, uvs, indices, 0);
        auto [tangent2, bitangent2] = GetTangentBitangent(verts, uvs, indices, 1);

        // create vertices for this face
        for (int i = 0; i < 6; i++) {
            auto& pos = verts[indices[i]];
            auto& normal = side.NormalForEdge(indices[i]);
            auto& uv = uvs[indices[i]];
            auto& color = colors[indices[i]];
            Vector2 uv2 = side.HasOverlay() ? ApplyOverlayRotation(side, uv) : Vector2();
            chunk.Center += pos;

            auto& tangent = i < 3 ? tangent1 : tangent2;
            auto& bitangent = i < 3 ? bitangent1 : bitangent2;
            LevelVertex vertex = { pos, uv, color, uv2, normal, tangent, bitangent, (int)tex1, (int)tex2 };
            geo.Vertices.push_back(vertex);
        }

        chunk.Center /= 4;
    }

    void Tessellate(Array<Vector3, 4>& verts,
                    LevelGeometry& geo,
                    LevelChunk& chunk,
                    SegmentSide& side,
                    int steps, 
                    TexID tex1,
                    TexID tex2) {
        auto incr = 1 / ((float)steps + 1);
        auto vTop = (verts[1] - verts[0]) * incr; // top
        auto vBottom = (verts[2] - verts[3]) * incr; // bottom

        auto uvTop = (side.UVs[1] - side.UVs[0]) * incr; // top
        auto uvBottom = (side.UVs[2] - side.UVs[3]) * incr; // bottom

        auto ltTop = (side.Light[1] - side.Light[0]) * incr; // top
        auto ltBottom = (side.Light[2] - side.Light[3]) * incr; // bottom

        // 1 step: 4 quads
        // 2 step: 9 quads
        // 3 step: 16 quads
        for (int x = 0; x < steps + 1; x++) {
            for (int y = 0; y < steps + 1; y++) {
                auto fx = (float)x, fy = (float)y;
                Array<Vector3, 4> p;
                auto edge0a = verts[0] + vTop * fx; // top left edge
                auto edge0b = verts[0] + vTop * (fx + 1); // top right edge
                auto edge1a = verts[3] + vBottom * fx; // bottom left edge
                auto edge1b = verts[3] + vBottom * (fx + 1); // bottom right edge
                auto vLeft = (edge1a - edge0a) * incr;
                auto vRight = (edge1b - edge0b) * incr;

                p[0] = edge0a + vLeft * fy; // top left
                p[1] = edge0b + vRight * fy; // top right
                p[2] = edge0b + vRight * (fy + 1); // bottom right
                p[3] = edge0a + vLeft * (fy + 1); // bottom left

                Array<Vector2, 4> uv;
                auto uvEdge0a = side.UVs[0] + uvTop * fx; // top left edge
                auto uvEdge0b = side.UVs[0] + uvTop * (fx + 1); // top right edge
                auto uvEdge1a = side.UVs[3] + uvBottom * fx; // bottom left edge
                auto uvEdge1b = side.UVs[3] + uvBottom * (fx + 1); // bottom right edge
                auto uvLeft = (uvEdge1a - uvEdge0a) * incr;
                auto uvRight = (uvEdge1b - uvEdge0b) * incr;

                uv[0] = uvEdge0a + uvLeft * fy; // top left
                uv[1] = uvEdge0b + uvRight * fy; // top right
                uv[2] = uvEdge0b + uvRight * (fy + 1); // bottom right
                uv[3] = uvEdge0a + uvLeft * (fy + 1); // bottom left

                Array<Color, 4> lt{};
                auto ltEdge0a = side.Light[0] + ltTop * fx; // top left edge
                auto ltEdge0b = side.Light[0] + ltTop * (fx + 1); // top right edge
                auto ltEdge1a = side.Light[3] + ltBottom * fx; // bottom left edge
                auto ltEdge1b = side.Light[3] + ltBottom * (fx + 1); // bottom right edge
                auto ltLeft = (ltEdge1a - ltEdge0a) * incr;
                auto ltRight = (ltEdge1b - ltEdge0b) * incr;

                lt[0] = ltEdge0a + ltLeft * fy; // top left
                lt[1] = ltEdge0b + ltRight * fy; // top right
                lt[2] = ltEdge0b + ltRight * (fy + 1); // bottom right
                lt[3] = ltEdge0a + ltLeft * (fy + 1); // bottom left

                AddPolygon(p, uv, lt, geo, chunk, side, tex1, tex2);
            }
        }
    }

    BlendMode GetWallBlendMode(LevelTexID id) {
        auto& mi = Resources::GetMaterial(id);
        return mi.Additive ? BlendMode::Additive : BlendMode::Alpha;
    }

    void CreateLevelGeometry(Level& level, ChunkCache& chunks, LevelGeometry& geo) {
        chunks.clear();
        geo.Chunks.clear();
        geo.Vertices.clear();
        geo.Walls.clear();

        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.Segments[id];
            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);
                auto isWall = seg.SideIsWall(sideId);

                // Do not render open sides
                if (seg.SideHasConnection(sideId) && !isWall)
                    continue;

                // Do not render the exit
                if (seg.GetConnection(sideId) == SegID::Exit)
                    continue;

                auto wall = level.TryGetWall(side.Wall);
                WallType wallType = wall ? wall->Type : WallType::None;

                // Do not render open walls
                if (isWall && wallType == WallType::Open)
                    continue;

                if (wallType == WallType::WallTrigger)
                    isWall = false; // wall triggers aren't really walls for the purposes of rendering

                // For sliding textures that have an overlay, we must store the overlay rotation sliding as well
                auto& ti = Resources::GetLevelTextureInfo(side.TMap);
                bool needsOverlaySlide = side.HasOverlay() && ti.Slide != Vector2::Zero;

                // pack the map ids together into a single integer (15 bits, 15 bits, 2 bits);
                uint16 overlayBit = needsOverlaySlide ? (uint16)side.OverlayRotation : 0;
                uint32 chunkId = (uint16)side.TMap | (uint16)side.TMap2 << 15 | overlayBit << 30;

                LevelChunk wallChunk; // always use a new chunk for walls
                LevelChunk& chunk = isWall ? wallChunk : chunks[chunkId];

                chunk.TMap1 = side.TMap;
                chunk.TMap2 = side.TMap2;
                chunk.EffectClip1 = Resources::GetEffectClipID(side.TMap);
                chunk.ID = id;

                if (side.HasOverlay())
                    chunk.EffectClip2 = Resources::GetEffectClipID(side.TMap2);

                Array<Color, 4> lt = side.Light;

                if (isWall && wall) {
                    chunk.Blend = GetWallBlendMode(side.TMap);
                    if (wall->Type == WallType::Cloaked) {
                        chunk.Blend = BlendMode::Alpha;
                        auto alpha = 1 - wall->CloakValue();
                        Seq::iter(lt, [alpha](auto& x) { x.A(alpha); });
                        chunk.Cloaked = true;
                    }
                }

                auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                auto tex1 = Resources::LookupTexID(side.TMap);
                auto tex2 = side.HasOverlay() ? Resources::LookupTexID(side.TMap2) : TexID::None;
                AddPolygon(verts, side.UVs, lt, geo, chunk, side, tex1, tex2);

                // Overlays should slide in the same direction as the base texture regardless of their rotation
                if (needsOverlaySlide)
                    chunk.OverlaySlide = ApplyOverlayRotation(side, ti.Slide);

                if (isWall) {
                    // Adjust wall positions to the center of the segment so objects and walls of a segment can be sorted correctly
                    chunk.Tag = { (SegID)id, sideId };
                    geo.Walls.push_back(chunk);
                }
            }
        }

        for (auto& chunk : chunks | views::values)
            geo.Chunks.push_back(chunk);
    }

    void LevelMesh::Draw(ID3D12GraphicsCommandList* cmdList) const {
        cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
        cmdList->IASetIndexBuffer(&IndexBuffer);
        cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
    }

    void LevelVolume::Draw(ID3D12GraphicsCommandList* cmdList) const {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Render::Adapter->GetGraphicsContext().ApplyEffect(Render::Effects->FlatAdditive);

        FlatShader::Constants constants;
        constants.Transform = Render::ViewProjection;
        constants.Tint = Color{ 1.00f, 0.6f, 0.01f, 0.66f };
        Render::Shaders->Flat.SetConstants(cmdList, constants);
        cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
        cmdList->IASetIndexBuffer(&IndexBuffer);
        cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
    }

    //void LevelMeshWorker::Work() {
    //    auto index = (_index + 1) % 2;
    //    auto& upload = _upload[index];
    //    auto& resources = _resources[index];
    //    resources = {};
    //    ChunkCache chunks;
    //    CreateLevelGeometry(_level, chunks, resources.Geometry);

    //    if (HasWork()) return;

    //    upload.Reset();
    //    // Upload the new geometry to the unused resource buffer
    //    auto vbv = upload.PackVertices(resources.Geometry.Vertices);
    //    if (HasWork()) return;

    //    for (auto& c : resources.Geometry.Chunks) {
    //        auto ibv = upload.PackIndices(c.Indices);
    //        resources.Meshes.push_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
    //    }

    //    if (HasWork()) return;

    //    for (auto& c : resources.Geometry.Walls) {
    //        auto ibv = upload.PackIndices(c.Indices);
    //        resources.WallMeshes.push_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
    //    }

    //    if (HasWork()) return;

    //    _hasNewData = true;
    //}

    void LevelMeshBuilder::Update(Level& level, PackedBuffer& buffer) {
        CreateLevelGeometry(level, _chunks, _geometry);
        UpdateBuffers(buffer);
    }

    void LevelMeshBuilder::UpdateBuffers(PackedBuffer& buffer) {
        buffer.ResetIndex();
        _meshes.clear();
        _wallMeshes.clear();

        auto vbv = buffer.PackVertices(_geometry.Vertices);

        for (auto& c : _geometry.Chunks) {
            auto ibv = buffer.PackIndices(c.Indices);
            _meshes.emplace_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }

        for (auto& c : _geometry.Walls) {
            auto ibv = buffer.PackIndices(c.Indices);
            _wallMeshes.emplace_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }
    }
}

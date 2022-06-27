#include "pch.h"
#include "LevelMesh.h"
#include "Render.h"

namespace Inferno {
    using namespace DirectX;

    constexpr bool TMapIsLava(LevelTexID id) {
        constexpr std::array tids = { 291, 378, 404, 405, 406, 407, 408, 409 };
        return Seq::contains(tids, (int)id);
    }

    bool SegHasLava(Segment& seg) {
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
        for (int sid = -1; auto & seg : level.Segments) {
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
                if (auto cseg = level.TryGetSegment(seg.GetConnection(sideId))) {
                    if (Seq::contains(heatSegs, segId) &&
                        !level.TryGetWall({ (SegID)segId, sideId }))
                        continue;
                }

                Array<FlatVertex, 4> sideVerts;

                bool isLit = false;

                for (int i = 0; auto & v : seg.GetVertexIndices(sideId)) {
                    sideVerts[i].Position = level.Vertices[v];
                    if (Seq::contains(heatIndices, v)) {
                        sideVerts[i].Color = { 1, 1, 1, 1 };
                        isLit = true;
                    }
                    else {
                        sideVerts[i].Color = { 1, 1, 1, 0 };
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

    Vector2 GetOverlayRotation(SegmentSide& side, Vector2 uv) {
        float overlayAngle = [&side]() {
            switch (side.OverlayRotation) {
                case OverlayRotation::Rotate0: default: return 0.0f;
                case OverlayRotation::Rotate90: return XM_PIDIV2;
                case OverlayRotation::Rotate180: return XM_PI;
                case OverlayRotation::Rotate270: return XM_PI * 1.5f;
            };
        }();

        return Vector2::Transform(uv, Matrix::CreateRotationZ(overlayAngle));
    }

    //inline void AddPolygon(Array<Vector3, 4>& verts, 
    //                       LevelGeometry& geo, 
    //                       LevelChunk& chunk, 
    //                       SegmentSide& side, 
    //                       float alpha) {
    //    auto startIndex = geo.Vertices.size();
    //    chunk.AddQuad((uint16)startIndex, side);

    //    auto light = side.Light;
    //    Seq::iter(light, [alpha](auto x) { x.A(alpha); });

    //    // create vertices for this face
    //    for (int i = 0; i < 4; i++) {
    //        chunk.Center += verts[i];
    //        auto& normal = side.AverageNormal; // todo: proper normal from split
    //        Vector2 uv2 = side.HasOverlay() ? GetOverlayRotation(side, side.UV[i]) : Vector2();
    //        DX::LevelVertex vertex = { verts[i], side.UV[i], side.Light[i], uv2, normal };

    //        geo.Vertices.push_back(vertex);
    //    }

    //    chunk.Center /= 4;
    //}

    inline void AddPolygon(Array<Vector3, 4>& verts,
                           Array<Vector2, 4>& uv,
                           Array<Color, 4>& lt,
                           LevelGeometry& geo,
                           LevelChunk& chunk,
                           SegmentSide& side) {
        auto startIndex = geo.Vertices.size();
        chunk.AddQuad((uint16)startIndex, side);

        // create vertices for this face
        for (int i = 0; i < 4; i++) {
            Vector3 pos = verts[i];
            chunk.Center += pos;

            // todo: pick normal 0 or 1 based on side split type
            auto& normal = side.AverageNormal;
            Vector2 uv2 = side.HasOverlay() ? GetOverlayRotation(side, uv[i]) : Vector2();
            LevelVertex vertex = { pos, uv[i], lt[i], uv2, normal };
            geo.Vertices.push_back(vertex);
        }

        chunk.Center /= 4;
    }


    //void UpdatePolygon(Level& level, Segment& seg, LevelGeometry& geo, LevelChunk& chunk, SideID sideId, SegmentSide& side) {
    //    auto face = Face::FromSide(level, seg, sideId);

    //    for (int i = 0; i < 4; i++) {
    //        chunk.Center += face[i];
    //        auto& normal = side.AverageNormal; // todo: proper normal from split
    //        Vector2 uv2 = side.HasOverlay() ? GetOverlayRotation(side, side.UV[i]) : Vector2();

    //        geo.Vertices[/*chunk.IndexOffset +*/ i] = { face[i], side.UV[i], side.Light[i], uv2, normal };
    //        geo.Vertices[/*chunk.IndexOffset +*/ i] = { face[i], side.UV[i], { 0, 0, 0, 0 }, uv2, normal };
    //    }

    //    chunk.Center /= 4;
    //    throw NotImplementedException();
    //}

    inline void Tessellate(Array<Vector3, 4>& verts,
                           LevelGeometry& geo,
                           LevelChunk& chunk,
                           SegmentSide& side,
                           int steps) {

        //float s = (float)steps + 1;

        //Vector3 pts[3][3]{};
        //pts[0][0] = verts[0];
        //pts[2][0] = verts[1];
        //pts[2][2] = verts[2];
        //pts[0][2] = verts[3];
        //pts[1][0] = (pts[0][0] + pts[2][0]) / 2.0f;
        //pts[2][1] = (pts[2][0] + pts[2][2]) / 2.0f;
        //pts[1][2] = (pts[2][2] + pts[0][2]) / 2.0f;
        //pts[0][1] = (pts[0][0] + pts[0][2]) / 2.0f;
        //pts[1][1] = (pts[1][0] + pts[1][2]) / 2.0f;

        //Vector2 uvs[3][3]{};
        //uvs[0][0] = side.UVs[0];
        //uvs[2][0] = side.UVs[1];
        //uvs[2][2] = side.UVs[2];
        //uvs[0][2] = side.UVs[3];
        //uvs[1][0] = (uvs[0][0] + uvs[2][0]) / 2.0f;
        //uvs[2][1] = (uvs[2][0] + uvs[2][2]) / 2.0f;
        //uvs[1][2] = (uvs[2][2] + uvs[0][2]) / 2.0f;
        //uvs[0][1] = (uvs[0][0] + uvs[0][2]) / 2.0f;
        //uvs[1][1] = (uvs[1][0] + uvs[1][2]) / 2.0f;

        //Color light[3][3]{};
        //light[0][0] = side.Light[0];
        //light[2][0] = side.Light[1];
        //light[2][2] = side.Light[2];
        //light[0][2] = side.Light[3];
        //light[1][0] = (light[0][0] + light[2][0]) * 0.5f;
        //light[2][1] = (light[2][0] + light[2][2]) * 0.5f;
        //light[1][2] = (light[2][2] + light[0][2]) * 0.5f;
        //light[0][1] = (light[0][0] + light[0][2]) * 0.5f;
        //light[1][1] = (light[1][0] + light[1][2]) * 0.5f;

        //for (int x = 0; x < 2; x++) {
        //    for (int y = 0; y < 2; y++) {
        //        Array<Vector3, 4> p{ pts[x][y], pts[x + 1][y], pts[x + 1][y + 1], pts[x][y + 1] };
        //        Array<Vector2, 4> uv{ uvs[x][y], uvs[x + 1][y], uvs[x + 1][y + 1], uvs[x][y + 1] };
        //        Array<Color, 4> lt{ light[x][y], light[x + 1][y], light[x + 1][y + 1], light[x][y + 1] };
        //        AddPolygon(p, uv, lt, geo, chunk, side);
        //    };
        //}

        //steps++;

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

                AddPolygon(p, uv, lt, geo, chunk, side);
            }
        }
    }

    BlendMode GetWallBlendMode(const Level& level, LevelTexID id) {
        if (level.IsDescent2()) {
            if (id == LevelTexID(353) ||
                id == LevelTexID(420) ||
                id == LevelTexID(432)) {
                return BlendMode::Additive;
            }
        }
        else if (level.IsDescent1()) {
            // energy field
            if (id == LevelTexID(328)) {
                return BlendMode::Additive;
            }
        }

        return BlendMode::Alpha;
    }


    //// Updates existing mesh data without allocations. Valid as long as the segment / wall count does not change.
    //inline void UpdateLevelGeometry(Level& level, Dictionary<int32, LevelChunk>& chunks, LevelGeometry& geo) {
    //    int id = 0;
    //    int wallId = 0;

    //    for (auto& seg : level.Segments) {
    //        for (auto& sideId : SideIDs) {
    //            auto& side = seg.GetSide(sideId);
    //            auto isWall = seg.SideIsWall(sideId);

    //            // Do not render open sides
    //            if (seg.SideHasConnection(sideId) && !isWall)
    //                continue;

    //            // Do not render the exit
    //            if (seg.GetConnection(sideId) == SegID::Exit)
    //                continue;

    //            WallType wallType = isWall ? level.GetWall(side.Wall).Type : WallType::None;

    //            // Do not render fly-through walls
    //            if (isWall && wallType == WallType::FlyThroughTrigger)
    //                continue;

    //            // pack the two map ids together into a single integer
    //            int32 chunkId = (int)side.TMap | (int)side.TMap2 << 16;

    //            LevelChunk* chunk = [&]() -> LevelChunk* {
    //                if (seg.SideHasConnection(sideId) && isWall) {
    //                    return &geo.Walls[wallId++];
    //                }
    //                else {
    //                    assert(chunks.contains(chunkId));
    //                    return &chunks[chunkId]; // Use existing chunk with tmaps
    //                }
    //            }();

    //            chunk->MapID1 = side.TMap;
    //            chunk->MapID2 = side.TMap2;
    //            chunk->Map1 = Resources::LookupLevelTexID(side.TMap);
    //            chunk->EffectClip1 = Resources::GetEffectClip(side.TMap);
    //            chunk->ID = id;

    //            if (side.HasOverlay()) {
    //                chunk->Map2 = Resources::LookupLevelTexID(side.TMap2);
    //                chunk->EffectClip2 = Resources::GetEffectClip(side.TMap2);
    //            }

    //            if (isWall) {
    //                chunk->Blend = GetWallBlendMode(level, side.TMap);
    //            }

    //            //auto verts = Face::FromSide(level, seg, sideId).CopyPoints();


    //            if (isWall && wallType != WallType::WallTrigger) {
    //                throw NotImplementedException();
    //                //SplitWall(verts, geo, *chunk, side);
    //            }
    //            else {
    //                UpdatePolygon(level, seg, geo, *chunk, sideId, side);
    //            }
    //        }
    //        id++;
    //    }

    //    int j = 0;
    //    for (auto& [key, chunk] : chunks)
    //        geo.Chunks[j++] = chunk;
    //}

    void CreateLevelGeometry(Level& level, ChunkCache& chunks, LevelGeometry& geo) {
        chunks.clear();
        geo.Chunks.clear();
        geo.Vertices.clear();
        geo.Walls.clear();

        int id = 0;

        for (auto& seg : level.Segments) {
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

                // Do not render fly-through walls
                if (isWall && wallType == WallType::FlyThroughTrigger)
                    continue;

                if (wallType == WallType::WallTrigger)
                    isWall = false; // wall triggers aren't really walls for the purposes of rendering

                // pack the two map ids together into a single integer
                int32 chunkId = (int)side.TMap | (int)side.TMap2 << 16;

                LevelChunk wallChunk; // hack
                LevelChunk* chunk = [&]() -> LevelChunk* {
                    if (seg.SideHasConnection(sideId) && isWall) {
                        return &wallChunk; // Walls always create a new chunk
                    }
                    else {
                        if (!chunks.contains(chunkId))
                            chunks[chunkId] = LevelChunk();

                        return &chunks[chunkId]; // Use existing chunk with tmaps
                    }
                }();

                chunk->MapID1 = side.TMap;
                chunk->MapID2 = side.TMap2;
                chunk->Map1 = Resources::LookupLevelTexID(side.TMap);
                chunk->EffectClip1 = Resources::GetEffectClip(side.TMap);
                chunk->ID = id;

                if (side.HasOverlay()) {
                    chunk->Map2 = Resources::LookupLevelTexID(side.TMap2);
                    chunk->EffectClip2 = Resources::GetEffectClip(side.TMap2);
                }

                float alpha = 1;
                if (isWall && wall) {
                    chunk->Blend = GetWallBlendMode(level, side.TMap);
                    if (wall->Type == WallType::Cloaked) {
                        chunk->Blend = BlendMode::Alpha;
                        alpha = wall->CloakValue();
                    }
                }

                auto verts = Face::FromSide(level, seg, sideId).CopyPoints();

                Array<Color, 4> lt = side.Light;
                Seq::iter(lt, [alpha](auto x) { x.A(alpha); });
                AddPolygon(verts, side.UVs, lt, geo, *chunk, side);

                if (isWall && wallType != WallType::WallTrigger) {
                    // Adjust wall positions to the center of the segment so objects and walls of a segment can be sorted correctly
                    auto vec = chunk->Center - seg.Center;
                    vec.Normalize();
                    chunk->Center = seg.Center /*+ vec*/;
                    geo.Walls.push_back(*chunk);
                }
            }
            id++;
        }

        for (auto& [key, chunk] : chunks)
            geo.Chunks.push_back(chunk);

        //geo.HeatVolumes = CreateHeatVolumes(level);
    }

    void LevelMesh::Draw(ID3D12GraphicsCommandList* cmdList) const {
        cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
        cmdList->IASetIndexBuffer(&IndexBuffer);
        cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
    }

    void LevelVolume::Draw(ID3D12GraphicsCommandList* cmdList) const {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Render::Effects->FlatAdditive.Apply(cmdList);
        FlatShader::Constants constants;
        constants.Transform = Render::ViewProjection;
        constants.Tint = { 1.00f, 0.6f, 0.01f, 0.66f };
        Render::Shaders->Flat.SetConstants(cmdList, constants);
        cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
        cmdList->IASetIndexBuffer(&IndexBuffer);
        cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
    }

    void LevelResources::Draw() {
        for (auto& mesh : Meshes)
            Render::DrawOpaque(Render::RenderCommand(&mesh, 0));

        for (auto& mesh : WallMeshes) {
            float depth = (mesh.Chunk->Center - Render::Camera.Position).LengthSquared();
            Render::DrawTransparent(Render::RenderCommand{ &mesh, depth });
        }
    }

    void LevelMeshWorker::Work() {
        auto index = (_index + 1) % 2;
        //SPDLOG_INFO("Updating index {}", index);
        auto& upload = _upload[index];
        auto& resources = _resources[index];
        resources = {};
        //DX::PackedUploadBuffer upload;
        //LevelResources2 resources;
        ChunkCache chunks;
        CreateLevelGeometry(_level, chunks, resources.Geometry);

        if (HasWork()) return;

        upload.Reset();
        // Upload the new geometry to the unused resource buffer
        auto vbv = upload.PackVertices(resources.Geometry.Vertices);
        if (HasWork()) return;

        for (auto& c : resources.Geometry.Chunks) {
            auto ibv = upload.PackIndices(c.Indices);
            resources.Meshes.push_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }

        if (HasWork()) return;

        for (auto& c : resources.Geometry.Walls) {
            auto ibv = upload.PackIndices(c.Indices);
            resources.WallMeshes.push_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }

        if (HasWork()) return;

        _hasNewData = true;
    }

    void LevelMeshBuilder::Update(Level& level, PackedBuffer& buffer) {

        //if (level.Segments.size() != _lastSegCount ||
        //    level.Vertices.size() != _lastVertexCount ||
        //    level.Walls.size() != _lastWallCount) {
        //    // full rebuild
        //    Geometry = CreateLevelGeometry(level, chunks);

        //    _lastSegCount = level.Segments.size();
        //    _lastVertexCount = level.Vertices.size();
        //    _lastWallCount = level.Walls.size();
        //}
        //else {
        //    // update
        //    UpdateLevelGeometry(level, chunks, Geometry);
        //}

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

#include "pch.h"
#include "LevelMesh.h"
#include "Game.Segment.h"

#include "MaterialLibrary.h"
#include "Procedural.h"
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
            for (auto& sideId : SIDE_IDS) {
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

            for (auto& sideId : SIDE_IDS) {
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
                    const Array<Vector3, 4>& lightDirs,
                    List<LevelVertex>& vertices,
                    LevelChunk& chunk,
                    const SegmentSide& side) {
        auto startIndex = vertices.size();
        chunk.AddQuad((uint16)startIndex);

        auto& indices = side.GetRenderIndices();
        auto [tangent1, bitangent1] = GetTangentBitangent(verts, uvs, indices, 0);
        auto [tangent2, bitangent2] = GetTangentBitangent(verts, uvs, indices, 1);

        // create vertices for this face
        for (int i = 0; i < 6; i++) {
            auto& pos = verts[indices[i]];
            //auto normal = &side.NormalForEdge(indices[i]);
            //auto& normal = side.AverageNormal;

            auto normal = i < 3 ? &side.Normals[0] : &side.Normals[1];

            if (side.Type == SideSplitType::Tri02 && (indices[i] == 0 || indices[i] == 2))
                normal = &side.AverageNormal;
            else if (side.Type == SideSplitType::Tri13 && (indices[i] == 1 || indices[i] == 3))
                normal = &side.AverageNormal;

            auto& uv = uvs[indices[i]];
            auto& lightDir = lightDirs[indices[i]];
            auto& color = colors[indices[i]];
            Vector2 uv2 = side.HasOverlay() ? ApplyOverlayRotation(side, uv) : Vector2();
            chunk.Center += pos;

            auto& tangent = i < 3 ? tangent1 : tangent2;
            auto& bitangent = i < 3 ? bitangent1 : bitangent2;
            //LevelVertex vertex = { pos, uv, color, uv2, normal, tangent, bitangent, (int)tex1, (int)tex2 };
            LevelVertex vertex = { pos, uv, color, uv2, *normal, tangent, bitangent, lightDir };
            vertices.push_back(vertex);
        }

        chunk.Center /= 4;
    }

    void Tessellate(Array<Vector3, 4>& verts,
                    Array<Vector3, 4>& lightDirs,
                    List<LevelVertex>& vertices,
                    LevelChunk& chunk,
                    SegmentSide& side,
                    int steps) {
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

                AddPolygon(p, uv, lt, lightDirs, vertices, chunk, side);
            }
        }
    }

    BlendMode GetWallBlendMode(LevelTexID id) {
        auto& mi = Resources::GetMaterial(id);
        return mi.Additive ? BlendMode::Additive : BlendMode::Alpha;
    }

    void UpdateBounds(LevelChunk& chunk, span<LevelVertex> vertices) {
        Vector3 min(FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (auto& index : chunk.Indices) {
            auto& v = vertices[index];
            min = Vector3::Min(v.Position, min);
            max = Vector3::Max(v.Position, max);
        }

        chunk.Bounds.Center = (min + max) / 2;
        chunk.Bounds.Extents = (max - min) / 2;
    }

    // unfinished UV fix for non-tiling textures. Emissive mipmaps still cause problems
    // and this UV shift causes a pixel loss around the border
    Array<Vector2, 4> FixEdgeUVs(const SegmentSide& side) {
        constexpr float UV_SHIFT = 1 / 200.0f;
        constexpr float EPS = 0.005f;
        Array<Vector2, 4> uvs = side.UVs;

        for (uint i = 0; i < 3; i++) {
            const auto& uv0 = side.UVs[i];
            const auto& uv1 = side.UVs[i + 1];
            auto& uv0d = uvs[i];
            auto& uv1d = uvs[i + 1];

            // Check if the edge is aligned on u, and that it is close to a whole number
            if (std::abs(uv0.x - uv1.x) < EPS && std::abs(uv0.x - std::round(uv0.x)) < EPS) {
                // which direction to make bigger?
                auto sign = Sign(uvs[(i + 2) % 4].x - uv0.x);

                // edge matches
                uv0d.x = uv0.x + UV_SHIFT * sign;
                uv1d.x = uv1.x + UV_SHIFT * sign;
            }

            // Check if the edge is aligned on v, and that it is close to a whole number
            if (std::abs(uv0.y - uv1.y) < EPS && std::abs(uv0.y - std::round(uv0.y)) < EPS) {
                auto sign = Sign(uvs[(i + 2) % 4].y - uv0.y);

                // edge matches
                uv0d.y = uv0.y + UV_SHIFT * sign;
                uv1d.y = uv1.y + UV_SHIFT * sign;
            }
        }

        return uvs;
    }

    void LevelMeshBuilder::CreateLevelGeometry(Level& level) {
        _chunks.clear();
        _decals.clear();
        _geometry.Chunks.clear();
        _geometry.Vertices.clear();
        //_geometry.Decals.clear();
        _geometry.Walls.clear();
        _geometry.Lights.clear();

        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.Segments[id];
            for (auto& sideId : SIDE_IDS) {
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
                auto& tmapi = Resources::Materials.GetMaterialInfo(side.TMap);
                bool isLight = tmapi.EmissiveStrength > 0 && tmapi.LightReceived != 0;

                if (!isLight && side.TMap2 > LevelTexID::None) {
                    auto& tmapi2 = Resources::Materials.GetMaterialInfo(side.TMap2);
                    isLight |= tmapi2.EmissiveStrength > 0 && tmapi2.LightReceived != 0;
                }

                Array<Vector2, 4> uvs = side.UVs;
                uvs = FixEdgeUVs(side);

                if (isWall || isLight) {
                    LevelChunk chunk; // always use a new chunk for walls
                    chunk.TMap1 = side.TMap;
                    chunk.TMap2 = side.TMap2;
                    chunk.EffectClip1 = Resources::GetEffectClipID(side.TMap);
                    chunk.ID = id;

                    auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                    //auto tex1 = Resources::LookupTexID(side.TMap);
                    //auto tex2 = side.HasOverlay() ? Resources::LookupTexID(side.TMap2) : TexID::None;

                    AddPolygon(verts, uvs, side.Light, side.LightDirs, _geometry.Vertices, chunk, side);
                    AddPolygon(verts, uvs, side.Light, side.LightDirs, _geometry.Vertices, chunk, side);

                    if (side.HasOverlay())
                        chunk.EffectClip2 = Resources::GetEffectClipID(side.TMap2);

                    if (isWall && wall) {
                        chunk.Blend = GetWallBlendMode(side.TMap);
                        if (wall->Type == WallType::Cloaked) {
                            chunk.Blend = BlendMode::Alpha;
                            auto alpha = 1 - wall->CloakValue();
                            Seq::iter(side.Light, [alpha](auto& x) { x.A(alpha); });
                            chunk.Cloaked = true;
                        }
                    }

                    // Overlays should slide in the same direction as the base texture regardless of their rotation
                    if (needsOverlaySlide)
                        chunk.OverlaySlide = ApplyOverlayRotation(side, ti.Slide);

                    chunk.Tag = { (SegID)id, sideId };

                    // Prioritize walls instead of lights, otherwise they won't be drawn correctly
                    if (isWall)
                        _geometry.Walls.push_back(chunk);
                    else
                        _geometry.Lights.push_back(chunk);
                }
                else {
                    // pack the map ids together into a single integer (15 bits, 15 bits, 2 bits);
                    uint16 overlayBit = needsOverlaySlide ? (uint16)side.OverlayRotation : 0;
                    uint32 chunkId = (uint16)side.TMap | (uint16)side.TMap2 << 15 | overlayBit << 30;

                    auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                    //auto tex1 = Resources::LookupTexID(side.TMap);
                    auto tex2 = side.HasOverlay() ? Resources::LookupTexID(side.TMap2) : TexID::None;

                    LevelChunk& chunk = _chunks[chunkId];
                    chunk.TMap1 = side.TMap;
                    chunk.TMap2 = side.TMap2;
                    // Don't cull overlay procedural textures as they are not handled properly by the renderer
                    chunk.SkipDecalCull = GetProcedural(tex2) != nullptr && Resources::GetTextureInfo(tex2).Transparent;
                    chunk.EffectClip1 = Resources::GetEffectClipID(side.TMap);
                    chunk.ID = id;

                    if (side.HasOverlay())
                        chunk.EffectClip2 = Resources::GetEffectClipID(side.TMap2);

                    AddPolygon(verts, uvs, side.Light, side.LightDirs, _geometry.Vertices, chunk, side);

                    // Overlays should slide in the same direction as the base texture regardless of their rotation
                    if (needsOverlaySlide)
                        chunk.OverlaySlide = ApplyOverlayRotation(side, ti.Slide);

                    // Split into decals and base textures. However this has problems with overdraw.
                    //{
                    //    LevelChunk& chunk = _chunks[(uint)side.TMap];
                    //    chunk.TMap1 = side.TMap;
                    //    chunk.EffectClip1 = Resources::GetEffectClipID(side.TMap);
                    //    chunk.ID = id;

                    //    auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                    //    auto tex1 = Resources::LookupTexID(side.TMap);
                    //    //auto tex2 = side.HasOverlay() ? Resources::LookupTexID(side.TMap2) : TexID::None;
                    //    AddPolygon(verts, side.UVs, side.Light, _geometry, chunk, side, tex1, TexID::None);
                    //}

                    if (side.HasOverlay()) {
                        //uint16 overlayBit = needsOverlaySlide ? (uint16)side.OverlayRotation : 0;
                        //uint32 chunkId = 0 | (uint16)side.TMap2 << 15 | overlayBit << 30;

                        LevelChunk& decalChunk = _decals[chunkId];
                        decalChunk.TMap2 = side.TMap2;
                        decalChunk.EffectClip2 = Resources::GetEffectClipID(side.TMap2);
                        decalChunk.ID = id;

                        // Overlays should slide in the same direction as the base texture regardless of their rotation
                        if (needsOverlaySlide)
                            decalChunk.OverlaySlide = ApplyOverlayRotation(side, ti.Slide);

                        //auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                        //auto tex2 = Resources::LookupTexID(side.TMap2);
                        AddPolygon(verts, uvs, side.Light, side.LightDirs, _geometry.Vertices, decalChunk, side);
                    }
                }
            }
        }

        for (auto& chunk : _chunks | views::values)
            _geometry.Chunks.push_back(chunk);

        //for (auto& decal : _decals | views::values)
        //    _geometry.Decals.push_back(decal);
    }

    void LevelMesh::Draw(ID3D12GraphicsCommandList* cmdList) const {
        cmdList->IASetVertexBuffers(0, 1, &VertexBuffer);
        cmdList->IASetIndexBuffer(&IndexBuffer);
        cmdList->DrawIndexedInstanced(IndexCount, 1, 0, 0, 0);
        Render::Stats::DrawCalls++;
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

    void LevelMeshBuilder::Update(Level& level, PackedBuffer& buffer) {
        CreateLevelGeometry(level);
        UpdateBuffers(buffer);
    }

    void LevelMeshBuilder::UpdateBuffers(PackedBuffer& buffer) {
        buffer.ResetIndex();
        _meshes.clear();
        _wallMeshes.clear();
        _decalMeshes.clear();

        auto vbv = buffer.PackVertices(_geometry.Vertices);

        for (auto& c : _geometry.Chunks) {
            UpdateBounds(c, _geometry.Vertices);
            auto ibv = buffer.PackIndices(c.Indices);
            _meshes.emplace_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }

        for (auto& c : _geometry.Lights) {
            UpdateBounds(c, _geometry.Vertices);
            auto ibv = buffer.PackIndices(c.Indices);
            _meshes.emplace_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }

        for (auto& c : _geometry.Walls) {
            UpdateBounds(c, _geometry.Vertices);
            auto ibv = buffer.PackIndices(c.Indices);
            _wallMeshes.emplace_back(LevelMesh{ vbv, ibv, (uint)c.Indices.size(), &c });
        }
    }
}

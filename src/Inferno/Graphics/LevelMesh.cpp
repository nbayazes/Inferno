#include "pch.h"
#include "LevelMesh.h"
#include "Render.h"

namespace Inferno {
    using namespace DirectX;

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

    void AddPolygon(const Array<Vector3, 4>& verts,
                    const Array<Vector2, 4>& uv,
                    const Array<Color, 4>& lt,
                    LevelGeometry& geo,
                    LevelChunk& chunk,
                    SegmentSide& side) {
        auto startIndex = geo.Vertices.size();
        chunk.AddQuad((uint32)startIndex, side);

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

    void Tessellate(Array<Vector3, 4>& verts,
                    LevelGeometry& geo,
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

                // Do not render fly-through walls
                if (isWall && wallType == WallType::FlyThroughTrigger)
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
                    chunk.Blend = GetWallBlendMode(level, side.TMap);
                    if (wall->Type == WallType::Cloaked) {
                        chunk.Blend = BlendMode::Alpha;
                        auto alpha = 1 - wall->CloakValue();
                        Seq::iter(lt, [alpha](auto& x) { x.A(alpha); });
                        chunk.Cloaked = true;
                    }
                }

                auto verts = Face::FromSide(level, seg, sideId).CopyPoints();
                AddPolygon(verts, side.UVs, lt, geo, chunk, side);

                // Overlays should slide in the same direction as the base texture regardless of their rotation
                if (needsOverlaySlide)
                    chunk.OverlaySlide = GetOverlayRotation(side, ti.Slide);

                if (isWall) {
                    // Adjust wall positions to the center of the segment so objects and walls of a segment can be sorted correctly
                    auto vec = chunk.Center - seg.Center;
                    vec.Normalize();
                    chunk.Center = seg.Center;
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
        Render::Effects->FlatAdditive.Apply(cmdList);
        FlatShader::Constants constants;
        constants.Transform = Render::ViewProjection;
        constants.Tint = Color{ 1.00f, 0.6f, 0.01f, 0.66f };
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

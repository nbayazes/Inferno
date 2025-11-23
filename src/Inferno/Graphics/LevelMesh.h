#pragma once

#include <array>
#include "Buffers.h"
#include "ShaderLibrary.h"
#include "Face.h"

namespace Inferno {
    // A chunk of level geometry grouped by texture maps
    struct LevelChunk {
        List<uint32> Indices; // Indices into the LevelGeometry buffer (NOT level vertices)
        LevelTexID TMap1, TMap2;
        uint ID = 0;
        EClipID EffectClip1 = EClipID::None;
        EClipID EffectClip2 = EClipID::None;
        Vector2 OverlaySlide; // UV sliding corrected for overlay rotation

        // Geometric center, used for wall depth sorting
        Vector3 Center;
        BlendMode Blend = BlendMode::Opaque;
        bool Cloaked = false;

        void AddQuad(uint32 index, const SegmentSide& side) {
            for (auto i : side.GetRenderIndices())
                Indices.push_back(index + i);
        }
    };


    struct LevelGeometry {
        // Static meshes
        List<LevelChunk> Chunks;
        // 'Wall' meshes that require depth sorting
        List<LevelChunk> Walls;
        // Technically vertices are no longer needed after being uploaded
        List<LevelVertex> Vertices;
    };

    using ChunkCache = Dictionary<uint32, LevelChunk>;

    struct LevelMesh {
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        uint IndexCount;
        const LevelChunk* Chunk = nullptr;

        void Draw(ID3D12GraphicsCommandList* cmdList) const;
    };

    struct LevelVolume {
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        uint IndexCount;

        void Draw(ID3D12GraphicsCommandList* cmdList) const;
    };

    struct LevelResources {
        LevelGeometry Geometry;
        List<LevelMesh> Meshes;
        List<LevelMesh> WallMeshes;

        // Submits meshes to the queue
        void Draw();
    };

    class LevelMeshBuilder {
        int _lastSegCount = 0, _lastVertexCount = 0, _lastWallCount = 0;

        LevelGeometry _geometry;
        List<LevelMesh> _meshes;
        List<LevelMesh> _wallMeshes;
        ChunkCache _chunks;
    public:
        List<LevelMesh>& GetMeshes() { return _meshes; }
        List<LevelMesh>& GetWallMeshes() { return _wallMeshes; }

        void Update(Level& level, PackedBuffer& buffer);


    private:
        void UpdateBuffers(PackedBuffer& buffer);
    };
}
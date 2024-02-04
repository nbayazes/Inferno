#pragma once

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
        bool SkipDecalCull = false; // Set to true when overlay is a transparent procedural
        Color LightColor; // Light color for decals

        // Geometric center used for wall depth sorting
        Vector3 Center;
        Tag Tag{}; // Only valid for walls
        BlendMode Blend = BlendMode::Opaque;
        bool Cloaked = false;
        DirectX::BoundingOrientedBox Bounds; // Only for walls

        void AddQuad(uint32 index) {
            for (uint32 i = 0; i < 6; i++) {
                Indices.push_back(index + i);
            }
        }
    };

    struct HeatVolume {
        List<uint16> Indices;
        List<FlatVertex> Vertices;
    };

    struct LevelGeometry {
        // Static meshes
        List<LevelChunk> Chunks;

        // Static mesh decals (overlay textures)
        //List<LevelChunk> Decals;

        // 'Wall' meshes that require depth sorting
        List<LevelChunk> Walls;

        // Sides with lights on them are unique so that they can be colored individually
        List<LevelChunk> Lights;

        // Technically vertices are no longer needed after being uploaded
        List<LevelVertex> Vertices;
        HeatVolume HeatVolumes;
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
    };

    //class LevelMeshWorker : public WorkerThread {
    //    PackedUploadBuffer _upload[2]{};
    //    LevelResources _resources[2]{};
    //    std::atomic<int> _index;
    //    Level _level;
    //    std::atomic<bool> _hasNewData = false;
    //public:
    //    auto& GetLevelResources() {
    //        //SPDLOG_INFO("Reading index {}", _index % 2);
    //        return _resources[_index % 2];
    //    }

    //    void CopyLevel(const Level& level) { _level = level; }

    //    //void Draw() {
    //    //    _resources[_index % 2].Draw();
    //    //}

    //    void SwapBuffer() {
    //        _hasNewData = false;
    //        _index++;
    //        //SPDLOG_INFO("Swapped to index {}", index);
    //    }

    //    bool HasNewData() { return _hasNewData; }

    //protected:
    //    void Work() override;
    //};


    class LevelMeshBuilder {
        int _lastSegCount = 0, _lastVertexCount = 0, _lastWallCount = 0;

        LevelGeometry _geometry;
        List<LevelMesh> _meshes;
        List<LevelMesh> _wallMeshes, _decalMeshes;
        ChunkCache _chunks, _decals;
    public:
        span<LevelMesh> GetMeshes() { return _meshes; }
        span<LevelMesh> GetDecals() { return _decalMeshes; }
        span<LevelMesh> GetWallMeshes() { return _wallMeshes; }

        void Update(Level& level, PackedBuffer& buffer);
    private:
        void CreateLevelGeometry(Level& level);
        void UpdateBuffers(PackedBuffer& buffer);
    };
}

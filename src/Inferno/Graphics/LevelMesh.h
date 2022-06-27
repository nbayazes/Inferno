#pragma once

#include <array>
#include "Buffers.h"
#include "ShaderLibrary.h"
#include "Face.h"

namespace Inferno {
    // A chunk of level geometry grouped by texture maps
    struct LevelChunk {
        List<uint16> Indices;
        TexID Map1 = TexID::None;
        TexID Map2 = TexID::None;
        LevelTexID MapID1, MapID2;
        uint ID = 0;
        EClipID EffectClip1 = EClipID::None;
        EClipID EffectClip2 = EClipID::None;

        // Geometric center, used for wall depth sorting
        Vector3 Center;
        BlendMode Blend = BlendMode::Opaque;

        void AddQuad(uint16 index, const SegmentSide& side) {
            for (auto i : side.GetRenderIndices())
                Indices.push_back(index + i);
        }
    };

    struct HeatVolume {
        List<uint16> Indices;
        List<FlatVertex> Vertices;
    };

    struct LevelGeometry {
        // Static meshes
        List<LevelChunk> Chunks;
        // 'Wall' meshes that require depth sorting
        List<LevelChunk> Walls;
        // Technically vertices are no longer needed after being uploaded
        List<LevelVertex> Vertices;
        HeatVolume HeatVolumes;
    };

    using ChunkCache = Dictionary<int32, LevelChunk>;

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

    class LevelMeshWorker : public WorkerThread {
        PackedUploadBuffer _upload[2]{};
        LevelResources _resources[2]{};
        std::atomic<int> _index;
        Level _level;
        std::atomic<bool> _hasNewData = false;
    public:
        auto& GetLevelResources() {
            //SPDLOG_INFO("Reading index {}", _index % 2);
            return _resources[_index % 2];
        }

        void CopyLevel(Level level) { _level = level; }

        void Draw() {
            _resources[_index % 2].Draw();
        }

        void SwapBuffer() {
            _hasNewData = false;
            _index++;
            //SPDLOG_INFO("Swapped to index {}", index);
        }

        bool HasNewData() { return _hasNewData; }

    protected:
        void Work() override;
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
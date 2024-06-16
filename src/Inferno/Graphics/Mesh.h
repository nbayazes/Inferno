#pragma once

#include "Buffers.h"
#include "logging.h"
#include "OutrageModel.h"
#include "Polymodel.h"
#include "VertexTypes.h"

namespace Inferno::Render {
    constexpr int VCLIP_RANGE = 10000; // Mesh TexIDs past this range are treated as vclips

    // An object mesh used for rendering
    struct Mesh {
        D3D12_INDEX_BUFFER_VIEW IndexBuffer;
        D3D12_VERTEX_BUFFER_VIEW VertexBuffer;
        uint IndexCount = 0;
        TexID Texture = TexID::None;
        string TextureName; // alternative to texture id
        EClipID EffectClip = EClipID::None;
        bool IsTransparent = false;
    };

    // Pointers to individual meshes in a polymodel
    struct MeshIndex {
        // A lookup of meshes based on submodel and then texture
        Dictionary<int, Dictionary<int, Mesh*>> Meshes;
        bool Loaded = false;
        bool IsTransparent = false;
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
        void LoadModel(ModelID id);

        // Loads a D3 model
        void LoadOutrageModel(const Outrage::Model& model, ModelID id);

        MeshIndex& GetHandle(ModelID id) {
            return _handles[(int)id];
        }

        MeshIndex& GetOutrageHandle(ModelID id) {
            return _handles[_capacity + (int)id];
        }
    };

    class TerrainMesh {
        Mesh _mesh{};
        List<Mesh> _satellites;
        PackedBuffer _buffer{ 1024 * 1024 * 2 };

    public:
        TerrainMesh() {}

        void AddTerrain(span<const ObjectVertex> verts, span<const uint16> indices, string_view texture) {
            _mesh.VertexBuffer = _buffer.PackVertices(verts);
            _mesh.IndexBuffer = _buffer.PackIndices(indices);
            _mesh.IndexCount = (uint)indices.size();
            _mesh.TextureName = texture;
        }

        void AddSatellite(span<const ObjectVertex> verts, span<const uint16> indices, string_view texture) {
            Mesh mesh;
            mesh.VertexBuffer = _buffer.PackVertices(verts);
            mesh.IndexBuffer = _buffer.PackIndices(indices);
            mesh.IndexCount = (uint)indices.size();
            mesh.TextureName = texture;
            _satellites.push_back(mesh);
        }

        const Mesh& GetTerrain() const { return _mesh; }

        span<const Mesh> GetSatellites() const { return _satellites; }
    };
}

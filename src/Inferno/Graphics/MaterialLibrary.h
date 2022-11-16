#pragma once

#include "Heap.h"
#include "Level.h"
#include "Resources.h"
#include "Buffers.h"
#include "Concurrent.h"

namespace Inferno::Render {
    struct Material2D {
        enum { Diffuse, SuperTransparency, Emissive, Specular, Count };

        // optional data for textures... should be moved
        Texture2D Textures[Count]{};
        // SRV handles
        D3D12_GPU_DESCRIPTOR_HANDLE Handles[Count] = {};
        uint Index = 0;
        TexID ID = TexID::Invalid;
        string Name;
    };

    struct MaterialUpload {
        TexID ID = TexID::None;
        const PigBitmap* Bitmap;
        bool SuperTransparent = false;
        bool ForceLoad = false;
        Outrage::Bitmap Outrage;
    };

    // Supports loading and unloading materials
    class MaterialLibrary {
        Material2D _defaultMaterial;

        bool _requestPrune = false;
        Texture2D _black, _white, _purple;
        ConcurrentList<Material2D> _materials, PendingCopies;
        ConcurrentList<MaterialUpload> RequestedUploads;
        Dictionary<string, Material2D> _unpackedMaterials;

        Ptr<WorkerThread> _worker;
        friend class MaterialUploadWorker;
    public:
        MaterialLibrary(size_t size);

        void Shutdown();
        Material2D White, Black;

        void LoadMaterials(span<const TexID> tids, bool forceLoad);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false);
        void Dispatch();

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if ((int)id > _materials.Size()) return _defaultMaterial;
            auto& material = _materials[(int)id];
            return material.ID > TexID::Invalid ? material : _defaultMaterial;
        }

        // Gets a material based on a D1/D2 level texture ID
        const Material2D& Get(LevelTexID tid) const {
            auto id = Resources::LookupLevelTexID(tid);
            return Get(id);
        }

        // Gets a material loaded from the filesystem based on name
        const Material2D& Get(const string& name) {
            if (!_unpackedMaterials.contains(name)) return _defaultMaterial;
            return _unpackedMaterials[name];
        };

        void LoadLevelTextures(const Inferno::Level& level, bool force);
        void LoadTextures(span<string> names);

        void Reload();

        bool PreloadDoors = true; // For editor previews

        // Unloads unused materials
        void Prune() { _requestPrune = true; }
        void Unload();

        // Materials to keep loaded after a prune
        Set<TexID> KeepLoaded;

    private:
        MaterialUpload PrepareUpload(TexID id, bool forceLoad);
        void PruneInternal();

        bool HasUnloadedTextures(span<const TexID> tids) {
            bool hasPending = false;
            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
                if (_materials[(int)id].ID == id) continue;
                hasPending = true;
                break;
            }

            return hasPending;
        }

        void LoadDefaults();
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    ComPtr<ID3D12CommandQueue> EndTextureUpload(DirectX::ResourceUploadBatch&);

    List<TexID> GetTexturesForModel(ModelID id);

    inline Ptr<MaterialLibrary> Materials;

}
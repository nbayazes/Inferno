#pragma once

#include <condition_variable>
#include "Heap.h"
#include "Level.h"
#include "Resources.h"
#include "Buffers.h"
#include "Concurrent.h"
#include "OutrageBitmap.h"
#include "OutrageModel.h"

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
        ConcurrentList<Material2D> _materials, _pendingCopies;
        ConcurrentList<MaterialUpload> _requestedUploads;
        Dictionary<string, Material2D> _outrageMaterials;
        Set<TexID> _submittedUploads; // textures submitted for async processing. Used to filter future requests.

        Ptr<WorkerThread> _worker;
        friend class MaterialUploadWorker;
    public:
        MaterialLibrary(size_t size);

        void Shutdown();
        Material2D White, Black;

        void LoadMaterials(span<const TexID> ids, bool forceLoad);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false);
        void Dispatch();

        // Returns the starting GPU handle for the material
        const Material2D& Get(TexID id) const {
            if ((int)id > _materials.Size()) return _defaultMaterial;
            auto& material = _materials[(int)id];
            return material.ID > TexID::Invalid ? material : _defaultMaterial;
        }

        const Material2D& Get(LevelTexID tid) const {
            auto id = Resources::LookupLevelTexID(tid);
            return Get(id);
        }

        const Material2D& GetOutrageMaterial(const string& name) {
            if (!_outrageMaterials.contains(name)) return _defaultMaterial;
            return _outrageMaterials[name];
        };

        void LoadLevelTextures(const Inferno::Level& level, bool force);

        void LoadOutrageModel(const Outrage::Model& model);

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
        //Option<Material2D> LoadMaterial(DirectX::ResourceUploadBatch& batch, TexID id);

        void LoadDefaults();
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    ComPtr<ID3D12CommandQueue> EndTextureUpload(DirectX::ResourceUploadBatch&);

    List<TexID> GetTexturesForModel(ModelID id);

    inline Ptr<MaterialLibrary> Materials;

    Set<TexID> GetLevelSegmentTextures(const Level& level);
}
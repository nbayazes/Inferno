#pragma once

#include "Heap.h"
#include "Level.h"
#include "Resources.h"
#include "Buffers.h"
#include "Concurrent.h"

namespace Inferno::Render {
    enum class TextureState {
        Vacant,   // Default state 
        Resident, // Texture is loaded
        PagingIn  // Texture is being loaded
    };

    extern const int MATERIAL_COUNT;

    struct Material2D {
        enum { Diffuse, SuperTransparency, Emissive, Specular, Normal, Count };

        Texture2D Textures[Count]{};
        // SRV handles
        D3D12_GPU_DESCRIPTOR_HANDLE Handles[Count] = {};
        uint UploadIndex = 0;
        TexID ID = TexID::Invalid;
        string Name;
        TextureState State = TextureState::Vacant;

        explicit operator bool() const { return State == TextureState::Resident; }
        UINT64 Pointer() const { return Handles[Diffuse].ptr; }

        // Returns the handle of the first texture in the material. Materials are created so that all textures are contiguous.
        // In most cases only the first handle is necessary.
        D3D12_GPU_DESCRIPTOR_HANDLE Handle() const { return Handles[Diffuse]; }
    };

    struct MaterialUpload {
        TexID ID = TexID::None;
        Outrage::Bitmap Outrage;
        const PigBitmap* Bitmap;
        bool SuperTransparent = false;
        bool ForceLoad = false;
    };

    constexpr auto MISSING_MATERIAL = TexID(2900);
    constexpr auto WHITE_MATERIAL = TexID(2901);
    constexpr auto BLACK_MATERIAL = TexID(2902);

    // Supports loading and unloading materials
    class MaterialLibrary {
        MaterialInfo _defaultMaterialInfo = {};
        bool _requestPrune = false;
        List<Material2D> _materials;
        ConcurrentList<Material2D> _pendingCopies;
        ConcurrentList<MaterialUpload> _requestedUploads;
        Dictionary<string, TexID> _namedMaterials;

        Ptr<WorkerThread> _worker;
        List<MaterialInfo> _materialInfo;

        friend class MaterialUploadWorker;

    public:
        MaterialLibrary(size_t size);

        void Shutdown();

        void LoadMaterials(span<const TexID> tids, bool forceLoad = false);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false);
        void Dispatch();

        const Material2D& White() const { return _materials[(int)WHITE_MATERIAL]; }
        const Material2D& Black() const { return _materials[(int)BLACK_MATERIAL]; }
        const Material2D& Missing() const { return _materials[(int)MISSING_MATERIAL]; }
        span<MaterialInfo> GetAllMaterialInfo() { return _materialInfo; }

        MaterialInfo& GetMaterialInfo(TexID id) {
            if (!Seq::inRange(_materialInfo, (int)id)) return _defaultMaterialInfo;
            return _materialInfo[(int)id];
        }

        void ResetMaterials() {
            for (auto& material : _materialInfo)
                material = {};
        }

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if (!Seq::inRange(_materials, (int)id)) return Missing();
            return _materials[(int)id];
        }

        Material2D& Get(TexID id) {
            if (!Seq::inRange(_materials, (int)id)) return _materials[(int)MISSING_MATERIAL];
            return _materials[(int)id];
        }

        const Material2D& Get(EClipID id, double time, bool critical) const {
            auto& eclip = Resources::GetEffectClip(id);
            if (eclip.TimeLeft > 0)
                time = eclip.VClip.PlayTime - eclip.TimeLeft;

            TexID tex = eclip.VClip.GetFrame(time);
            if (critical && eclip.CritClip != EClipID::None) {
                auto& crit = Resources::GetEffectClip(eclip.CritClip);
                tex = crit.VClip.GetFrame(time);
            }

            return Get(tex);
        }

        // Gets a material based on a D1/D2 level texture ID
        const Material2D& Get(LevelTexID tid) const {
            auto id = Resources::LookupTexID(tid);
            return Get(id);
        }

        // Gets a material loaded from the filesystem based on name
        const Material2D& Get(const string& name) {
            auto id = Find(name);
            if (id == TexID::None) return Missing();
            return _materials[(int)id];
        }

        const TexID Find(const string& name) {
            auto item = _namedMaterials.find(name);
            return item == _namedMaterials.end() ? TexID::None : item->second;
        }

        void LoadLevelTextures(const Inferno::Level& level, bool force);
        void LoadTextures(span<const string> names);

        // Tries to load a texture and returns true if it exists
        bool LoadTexture(const string& name) {
            std::array tex = { name };
            LoadTextures(tex);
            return _namedMaterials[name] != TexID::None;
        }

        void Reload();

        bool PreloadDoors = true; // For editor previews

        // Unloads unused materials
        void Prune() { _requestPrune = true; }
        void Unload();

        // Materials to keep loaded after a prune
        Set<TexID> KeepLoaded;

    private:
        Option<MaterialUpload> PrepareUpload(TexID id, bool forceLoad);
        void PruneInternal();
        static void ResetMaterial(Material2D& material);

        // Returns true if any tids are unloaded
        bool HasUnloadedTextures(span<const TexID> tids) const {
            for (auto& id : tids) {
                if (id <= TexID::None) continue;
                if (_materials[(int)id].State != TextureState::Vacant) continue;
                return true;
            }

            return false;
        }

        void LoadDefaults();

        static constexpr auto LOOSE_TEXID_START = TexID(2905);
        TexID _looseTexId = LOOSE_TEXID_START;
        // returns a texid reserved for loose textures
        TexID GetUnusedTexID() {
            _looseTexId = TexID((int)_looseTexId + 1);
            assert((int)_looseTexId < MATERIAL_COUNT);
            return _looseTexId;
        }
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    void EndTextureUpload(DirectX::ResourceUploadBatch&, ID3D12CommandQueue*);

    List<TexID> GetTexturesForModel(ModelID id);
    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors);

    inline Ptr<MaterialLibrary> Materials;
    Set<TexID> GetLevelSegmentTextures(const Level& level);
}

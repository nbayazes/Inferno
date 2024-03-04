#pragma once

#include "Concurrent.h"
#include "Level.h"
#include "Material2D.h"
#include "OutrageBitmap.h"

namespace Inferno::Render {

    extern const uint MATERIAL_COUNT;

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
    constexpr auto SHINY_FLAT_MATERIAL = TexID(2903); // For flat untextured polygons on models

    // Supports loading and unloading materials
    class MaterialLibrary {
        List<Material2D> _materials;
        List<int8> _keepLoaded;
        List<Material2D> _pendingCopies;
        ConcurrentList<MaterialUpload> _requestedUploads;
        Dictionary<string, TexID> _namedMaterials;

        Ptr<WorkerThread> _worker;
        std::condition_variable _pruneCondition;

        friend class MaterialUploadWorker;

    public:
        MaterialLibrary(size_t size);

        void Shutdown();

        void LoadMaterials(span<const TexID> tids, bool forceLoad = false, bool keepLoaded = false);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);
        void Dispatch();

        const Material2D& White() const { return _materials[(int)WHITE_MATERIAL]; }
        const Material2D& Black() const { return _materials[(int)BLACK_MATERIAL]; }
        const Material2D& Missing() const { return _materials[(int)MISSING_MATERIAL]; }

        TextureCube EnvironmentCube;

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if (!Seq::inRange(_materials, (int)id)) return Missing();
            return _materials[(int)id];
        }

        Material2D& Get(TexID id) {
            if (!Seq::inRange(_materials, (int)id)) return _materials[(int)MISSING_MATERIAL];
            return _materials[(int)id];
        }

        const Material2D& Get(EClipID id, double time, bool critical) const;

        // Gets a material based on a D1/D2 level texture ID
        const Material2D& Get(LevelTexID tid) const;

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
        void LoadGameTextures();

        // Tries to load a texture and returns true if it exists
        bool LoadTexture(const string& name) {
            std::array tex = { name };
            LoadTextures(tex);
            return _namedMaterials[name] != TexID::None;
        }

        void Reload();

        bool PreloadDoors = true; // For editor previews

        // Unloads unused materials
        void Prune();
        void Unload();
        void UnloadNamedTextures();

    private:
        Option<MaterialUpload> PrepareUpload(TexID id, bool forceLoad);
        static void ResetMaterial(Material2D& material);

        // Returns true if any tids are unloaded
        bool HasUnloadedTextures(span<const TexID> tids) const {
            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
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
            assert((uint)_looseTexId < MATERIAL_COUNT);
            return _looseTexId;
        }
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    void EndTextureUpload(DirectX::ResourceUploadBatch&, ID3D12CommandQueue*);

    void GetTexturesForModel(ModelID id, Set<TexID>& ids);
    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors);
    Set<TexID> GetLevelSegmentTextures(const Level& level);

    inline Ptr<MaterialLibrary> Materials;
}

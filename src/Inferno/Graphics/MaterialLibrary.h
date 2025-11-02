#pragma once

#include "Concurrent.h"
#include "Level.h"
#include "Material2D.h"
#include "OutrageBitmap.h"
#include "Resources.Common.h"
#include "TextureCache.h"

namespace Inferno::Render {

    extern const uint MATERIAL_COUNT;

    struct MaterialUpload {
        TexID ID = TexID::None;
        Outrage::Bitmap Outrage;
        PigBitmap Bitmap;
        bool SuperTransparent = false;
        bool ForceLoad = false;
    };

    constexpr auto MISSING_MATERIAL = TexID(2900);
    constexpr auto WHITE_MATERIAL = TexID(2901);
    constexpr auto BLACK_MATERIAL = TexID(2902);
    constexpr auto SHINY_FLAT_MATERIAL = TexID(2903); // For flat untextured polygons on models
    constexpr auto TRANSPARENT_MATERIAL = TexID(2904);

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

        ~MaterialLibrary() = default;
        MaterialLibrary(const MaterialLibrary&) = delete;
        MaterialLibrary(MaterialLibrary&&) = delete;
        MaterialLibrary& operator=(const MaterialLibrary&) = delete;
        MaterialLibrary& operator=(MaterialLibrary&&) = delete;

        void Shutdown();

        void LoadMaterials(span<const TexID> tids, bool forceLoad = false, bool keepLoaded = false);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false, bool keepLoaded = false);
        void Dispatch();

        const Material2D& White() const { return _materials[(int)WHITE_MATERIAL]; }
        const Material2D& Black() const { return _materials[(int)BLACK_MATERIAL]; }
        const Material2D& Missing() const { return _materials[(int)MISSING_MATERIAL]; }
        const Material2D& Transparent() const { return _materials[(int)TRANSPARENT_MATERIAL]; }

        TextureCube EnvironmentCube;
        Texture2D Matcap;

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if (!Seq::inRange(_materials, (int)id)) return Missing();
            return _materials[(int)id];
        }

        Material2D& Get(TexID id) {
            if (!Seq::inRange(_materials, (int)id)) return _materials[(int)MISSING_MATERIAL];
            return _materials[(int)id];
        }

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
        void LoadTextures(span<const string> names, LoadFlag loadFlags = LoadFlag::Default, bool force = false);
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

        static constexpr auto NAMED_TEXID_START = TexID(2905);
        TexID _looseTexId = TexID(2905);

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

    // Returns all textures used by a level, including robots
    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors, bool includeAnimations = true);

    // Returns all textures applied to level segments, including animated frames
    Set<TexID> GetLevelSegmentTextures(const Level& level, bool includeAnimations = true);

    inline Ptr<MaterialLibrary> Materials;
}

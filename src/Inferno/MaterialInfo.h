#pragma once

#include "Procedural.h"
#include "Resources.Common.h"
#include "Types.h"

namespace Inferno {
    enum class MaterialFlags : int32 {
        None = 0,
        Additive = 1 << 1, // Additive blending
        WrapU = 1 << 2, // Marks this material as wrapping on the U axis
        WrapV = 1 << 3, // Marks this material as wrapping on the V axis
        Default = WrapU | WrapV,
    };

    // Must match MaterialInfo HLSL
    struct GpuMaterialInfo {
        float NormalStrength = 1; // multiplier on normal map
        float SpecularStrength = 1; // multiplier on specular
        // other map generation options like contrast, brightness?
        float Metalness = 0; // How much diffuse to apply to specular
        float Roughness = 0.6f; // 0 is sharp specular, 1 is no specular
        float EmissiveStrength = 0;
        float LightReceived = 1; // 0 for unlit
        int32 ID = -1; // TexID
        MaterialFlags Flags = MaterialFlags::Default;
        Color SpecularColor = Color(1, 1, 1, 1);
    };

    struct MaterialInfo : GpuMaterialInfo {
        Outrage::ProceduralInfo Procedural{};
        bool Modified = false; // Modified in the material editor
        string Name; // used to resolve the entry
        TableSource Source{}; // where this material was loaded from. Used by the editor to reset the definition.
    };

    class MaterialInfoLibrary {
        MaterialInfo _defaultMaterialInfo = {};
        List<MaterialInfo> _materialInfo;
        List<GpuMaterialInfo> _gpuMaterialInfo;

    public:
        MaterialInfoLibrary(size_t capacity = 0) : _materialInfo(capacity), _gpuMaterialInfo(capacity) {}

        span<MaterialInfo> GetAllMaterialInfo() { return _materialInfo; }

        // Rebuilds the GPU specific info from the current materials
        void RebuildGpuInfo() {
            _gpuMaterialInfo.resize(_materialInfo.size());

            for (size_t i = 0; i < _materialInfo.size(); i++) {
                _gpuMaterialInfo[i] = _materialInfo[i];
            }
        }

        span<GpuMaterialInfo> GetGpuMaterialInfo() { return _gpuMaterialInfo; }

        MaterialInfo& GetMaterialInfo(TexID id) {
            if (!Seq::inRange(_materialInfo, (int)id)) return _defaultMaterialInfo;
            return _materialInfo[(int)id];
        }
    };

    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials);
    List<MaterialInfo> LoadMaterialTable(const string& yaml);

    class MaterialTable {
        List<MaterialInfo> _materials;

    public:
        //void Merge(span<MaterialInfo> source) {
        //    for (auto& material : source) {
        //        AddOrUpdate(material, material.Name);
        //    }
        //}

        bool IsModified(const MaterialTable& original) const {
            // Check for deletions or additions
            if (_materials.size() != original._materials.size())
                return true;

            // Check for individual material changes
            for (auto& material : _materials) {
                if (material.Modified) {
                    return true;
                }
            }

            return false;
        }

        MaterialInfo* Find(string_view name) {
            return Seq::find(_materials, [name](const MaterialInfo& mat) {
                return mat.Name == name;
            });
        }

        span<MaterialInfo> Data() { return _materials; }
        span<const MaterialInfo> Data() const { return _materials; }

        // Gets a material or creates a new default material with the given name
        MaterialInfo& GetOrAdd(string_view name) {
            ASSERT(!name.empty());

            if (auto existing = Find(name)) {
                return *existing;
            }
            else {
                auto& material = _materials.emplace_back();
                material.Name = name;
                return material;
            }
        }

        bool Erase(string_view name) {
            return std::erase_if(_materials, [name](auto& mat) { return mat.Name == name; }) > 0;
        }

        // Adds a material or updates an existing material using the given name
        MaterialInfo& AddOrUpdate(const MaterialInfo& info, string_view name) {
            MaterialInfo material = info;
            material.Name = name;

            if (auto existing = Find(name)) {
                *existing = material;
                return *existing;
            }
            else {
                return _materials.emplace_back(material);
            }
        }

        void Save(std::ostream& stream) {
            SaveMaterialTable(stream, _materials);
        }

        static MaterialTable Load(const string& yaml, TableSource source) {
            MaterialTable table;
            table._materials = LoadMaterialTable(yaml);

            for (auto& material : table._materials) {
                material.Source = source;
            }

            return table;
        }
    };

    // Similar to material table, but assigns named textures to specific indices
    class IndexedMaterialTable {
        List<MaterialInfo> _materials;

    public:
        void Add(MaterialInfo& material);

        void Merge(MaterialTable& table) {
            for (auto& material : table.Data()) {
                Add(material);
            }
        }

        void Reset(size_t capacity) {
            _materials.clear();
            _materials.resize(capacity);
        }

        span<MaterialInfo> Data() { return _materials; }

        // Copies the first frame of an animation to the others.
        // If an id is provided, only that texture is expanded.
        void ExpandAnimatedFrames(TexID id = TexID::None);
    };

    // Materials loaded from the game data folders. Only contains entries that exist in the file.
    // Refer to Resources::Materials for the merged table
    inline MaterialTable Descent1Materials, Descent2Materials, MissionMaterials, LevelMaterials;
}

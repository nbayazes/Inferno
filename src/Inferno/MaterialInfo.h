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

    using MaterialTable = List<MaterialInfo>;

    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials);
    MaterialTable LoadMaterialTable(const string& yaml);

    // Materials loaded from the game data folders. Only contains entries that exist in the file.
    // Refer to Resources::Materials for the merged table
    inline MaterialTable Descent1Materials, Descent2Materials, MissionMaterials, LevelMaterials;
}

#pragma once

#include "Types.h"

namespace Inferno {
    enum class MaterialFlags : int32 {
        None = 0,
        Additive = 1 << 1, // Additive blending
        WrapU = 1 << 2, // Marks this material as not wrapping on the U axis
        WrapV = 1 << 3, // Marks this material as not wrapping on the V axis
        Default = WrapU | WrapV,
    };

    // Must match MaterialInfo HLSL
    struct MaterialInfo {
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

    struct MaterialInfoExt : MaterialInfo {
        bool WrapU = true; // Indicates that this texture wraps on the X direction. Used for texture clamping.
        bool WrapV = true; // Indicates that this texture wraps on the Y direction. Used for texture clamping.
    };

    class MaterialInfoLibrary {
        MaterialInfo _defaultMaterialInfo = {};
        List<MaterialInfo> _materialInfo;

    public:
        MaterialInfoLibrary(size_t capacity = 0) : _materialInfo(capacity) {}

        span<MaterialInfo> GetAllMaterialInfo() { return _materialInfo; }
        MaterialInfo& GetMaterialInfo(TexID id);
        MaterialInfo& GetMaterialInfo(LevelTexID id);
    };

    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials);
    void LoadMaterialTable(const string& yaml, span<MaterialInfo> materials);
}

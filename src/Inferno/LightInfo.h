#pragma once
#include "Types.h"

// Functionality related to loading extended light data such as default colors
// and point lights
namespace Inferno {
    // This must match the light shaders
    enum class LightType : uint32 {
        Point = 0,
        Tube = 1,
        Rectangle = 2,
        Spot = 3
    };

    enum class LightWrapMode {
        None = 0,
        U = 1,
        V = 2,
        //EdgeU = 3,
        //EdgeV = 4 // Wraps on V, but combines the first and last light when texture wraps on U
    };

    // Defines dynamic light sources on a texture
    struct TextureLightInfo {
        string Name;
        LevelTexID Id = LevelTexID::None;
        LightType Type = LightType::Point;
        List<Vector2> Points = { { 0.5f, 0.5f } }; // UV positions for each light
        float Offset = 0; // light surface offset
        float Radius = 40; // light radius
        float Width = 0.25f; // U Width for rectangular lights. For wrapped lights this is aligned to the wrap direction.
        float Height = 0.25f; // V Height for rectangular lights. Unused for wrapped lights.
        float Angle0 = 0; // Spotlight parameter. 1 / (cos inner - cos outer)
        float Angle1 = 0; // Spotlight parameter. cos outer
        Color Color = LIGHT_UNSET;
        LightWrapMode Wrap = LightWrapMode::None;

        bool IsContinuous() const {
            if (Points.size() != 2 || Type == LightType::Point) return false;
            return (Points[0].x == 0 && Points[1].x == 1) || (Points[0].y == 0 && Points[1].y == 1);
        }
    };

    // Loads light info from a YAML file
    void LoadLightTable(const string& yaml, List<TextureLightInfo>& lightInfo);

    // Loads light info from a YAML file
    void SaveLightTable(std::ostream& stream, const Dictionary<LevelTexID, TextureLightInfo>& lightInfo);
}

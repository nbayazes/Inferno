#include "pch.h"
#include "Yaml.h"
#include "LightInfo.h"

using namespace Yaml;

namespace Inferno {
    TextureLightInfo LoadTextureLightInfo(ryml::NodeRef node) {
        TextureLightInfo info;
        ReadValue(node["ID"], (int&)info.Id);
        ReadValue(node["Type"], (int&)info.Type);
        ReadValue(node["Wrap"], (int&)info.Wrap);

        auto pointNode = node["Points"];
        if (pointNode.valid() && !pointNode.is_seed()) {
            info.Points.clear();

            if (pointNode.has_children()) {
                // Array of points
                for (const auto& point : pointNode.children()) {
                    Vector2 uv;
                    ReadValue(point, uv);
                    info.Points.push_back(uv);
                }
            }
            else if (pointNode.has_val()) {
                // Single point
                Vector2 uv;
                ReadValue(pointNode, uv);
                info.Points.push_back(uv);
            }
        }

        ReadValue(node["Offset"], info.Offset);
        ReadValue(node["Radius"], info.Radius);
        ReadValue(node["Width"], info.Width);
        ReadValue(node["Height"], info.Height);
        ReadValue(node["Color"], info.Color);
        return info;
    }

    LevelLightInfo LevelLightInfo::Load(const string& data) {
        LevelLightInfo lightInfo;
        try {
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(data));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                auto textureNode = root["Textures"];
                if (textureNode.valid() && !textureNode.is_seed()) {
                    for (const auto& node : textureNode.children()) {
                        if (!node.valid() || node.is_seed()) continue;
                        auto info = LoadTextureLightInfo(node);
                        if (lightInfo.Textures.contains(info.Id))
                            SPDLOG_WARN("Redefined texture {} due to duplicate entry", (int)info.Id);

                        lightInfo.Textures[info.Id] = info;
                    }
                }
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }

        return lightInfo;
    }
}

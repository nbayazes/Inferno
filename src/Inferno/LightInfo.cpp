#include "pch.h"
#include "Yaml.h"
#include "LightInfo.h"
#include "Procedural.h"
#include "Resources.h"

using namespace Yaml;

namespace Inferno {
    TextureLightInfo ReadLightInfo(ryml::NodeRef node) {
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

    void SaveLightInfo(ryml::NodeRef node, const TextureLightInfo& info) {
        node |= ryml::MAP;
        node["ID"] << (int)info.Id;
        node["Type"] << (int)info.Type;
        node["Wrap"] << (int)info.Wrap;

        node["Points"] |= ryml::SEQ;
        for (auto& p : info.Points) {
            auto child = node["Points"].append_child();
            child << EncodeVector(p);
        }

        node["Offset"] << info.Offset;
        node["Radius"] << info.Radius;
        node["Width"] << info.Width;
        node["Height"] << info.Height;
        node["Color"] << EncodeColor3(info.Color);
    }

    Dictionary<LevelTexID, TextureLightInfo> LoadLightTable(const string& yaml) {
        Dictionary<LevelTexID, TextureLightInfo> lightInfo;

        try {
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                auto levelTextureNode = root["Lights"];
                if (levelTextureNode.valid() && !levelTextureNode.is_seed()) {
                    for (const auto& node : levelTextureNode.children()) {
                        if (!node.valid() || node.is_seed()) continue;
                        auto info = ReadLightInfo(node);
                        if (lightInfo.contains(info.Id))
                            SPDLOG_WARN("Redefined texture {} due to duplicate entry", (int)info.Id);

                        lightInfo[info.Id] = info;
                    }
                }
            }

            SPDLOG_INFO("Loaded {} light definitions", lightInfo.size());
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }

        return lightInfo;
    }

    
    void SaveLightTable(std::ostream& stream, const Dictionary<LevelTexID, TextureLightInfo>& lightInfo) {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            auto lightInfoNode = doc["LightInfo"];
            lightInfoNode |= ryml::SEQ;
            for (const auto& light : lightInfo | views::values) {
                auto node = lightInfoNode.append_child();
                SaveLightInfo(node, light);
            }

            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
        }
    }
}

#include "pch.h"
#include "Yaml.h"
#include "LightInfo.h"
#include "Procedural.h"
#include "Resources.h"

using namespace Yaml;

namespace Inferno {
    TextureLightInfo ReadLightInfo(ryml::NodeRef node) {
        TextureLightInfo info;
        ReadValue(node["Name"], info.Name);

        info.Id = Resources::FindLevelTexture(info.Name);

        ReadValue(node["Type"], (int&)info.Type);
        ReadValue(node["Wrap"], (int&)info.Wrap);

        float angle = 0, innerAngle = 0;
        ReadValue(node["Angle"], angle);
        ReadValue(node["InnerAngle"], innerAngle);

        float coneSpill = 0.1; // 10% spill by default
        ReadValue(node["ConeSpill"], coneSpill);
        info.ConeSpill = coneSpill;

        if (angle > 0) {
            info.Angle0 = 1.0f / (cosf(DegToRad * innerAngle) - cosf(DegToRad * angle));
            info.Angle1 = cosf(DegToRad * angle);
        }

        auto pointNode = node["Points"];
        if (pointNode.readable()) {
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
        node["Color"] << EncodeColor(info.Color);
    }

    void LoadLightTable(const string& yaml, List<TextureLightInfo>& lightInfo) {
        try {
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
            ryml::NodeRef root = doc.rootref();

            if (!root.is_map()) return;

            if (auto node = root["Lights"]; node.readable()) {
                for (const auto& child : node.children()) {
                    if (!child.readable()) continue;
                    auto info = ReadLightInfo(child);
                    lightInfo.push_back(info);
                }
            }

            SPDLOG_INFO("Loaded {} light definitions", lightInfo.size());
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }
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

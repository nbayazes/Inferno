#include "pch.h"
#include "Yaml.h"
#include "LightInfo.h"
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

    void ReadMaterialInfo(ryml::NodeRef node, span<MaterialInfo> materials) {
        if (!node.valid() || node.is_seed()) return;

        MaterialInfo info{};
        ReadValue(node["TexID"], info.ID);
        if (info.ID >= Resources::GameData.LevelTexIdx.size())
            return; // out of range

        ReadValue(node["NormalStrength"], info.NormalStrength);
        ReadValue(node["SpecularStrength"], info.SpecularStrength);
        ReadValue(node["Metalness"], info.Metalness);
        ReadValue(node["Roughness"], info.Roughness);
        ReadValue(node["EmissiveStrength"], info.EmissiveStrength);
        ReadValue(node["LightReceived"], info.LightReceived);

        materials[info.ID] = info;

        //auto& eclip = Resources::GetEffectClip(id);
        //for (auto frame : eclip.VClip.GetFrames()) {
        //    materials[frame] = info; // copy info to frames of the effect if present
        //}

        auto dclipId = Resources::GetDoorClipID(Resources::LookupLevelTexID((TexID)info.ID));
        auto& dclip = Resources::GetDoorClip(dclipId);

        // copy info to frames of door
        info.ID = -1; // unset ID so it doesn't get saved later for individual frames

        for (int i = 1; i < dclip.NumFrames; i++) {
            auto frameId = Resources::LookupTexID(dclip.Frames[i]);
            if (Seq::inRange(materials, (int)frameId)) {
                materials[(int)frameId] = info;
            }
        }
    }

    void SaveMaterialInfo(c4::yml::NodeRef& node, TexID id, const MaterialInfo& info) {
        node |= ryml::MAP;
        node["TexID"] << (int)id;
        node["NormalStrength"] << info.NormalStrength;
        node["SpecularStrength"] << info.SpecularStrength;
        node["Metalness"] << info.Metalness;
        node["Roughness"] << info.Roughness;
        node["EmissiveStrength"] << info.EmissiveStrength;
        node["LightReceived"] << info.LightReceived;
    }

    Dictionary<LevelTexID, TextureLightInfo> LoadLightTable(const string& yaml) {
        Dictionary<LevelTexID, TextureLightInfo> lightInfo;

        try {
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(yaml));
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

    void LoadMaterialTable(const string& yaml, span<MaterialInfo> materials) {
        try {
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(yaml));
            ryml::NodeRef root = doc.rootref();
            int count = 0;

            if (root.is_map()) {
                auto materialNode = root["Materials"];
                if (materialNode.valid() && !materialNode.is_seed()) {
                    for (const auto& node : materialNode.children()) {
                        ReadMaterialInfo(node, materials);
                        count++;
                    }
                }
            }
            SPDLOG_INFO("Loaded {} material definitions", count);
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }
    }

    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials) {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            doc["Materials"] |= ryml::SEQ;
            for (int i = 0; i < materials.size(); i++) {
                if (materials[i].ID == -1) continue;
                auto node = doc["Materials"].append_child();
                SaveMaterialInfo(node, (TexID)i, materials[i]);
            }

            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
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

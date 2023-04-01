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

    void ReadMaterialInfo(ryml::NodeRef node, Dictionary<TexID, MaterialInfo>& materials) {
        if (!node.valid() || node.is_seed()) return;

        MaterialInfo info{};
        TexID id;
        ReadValue(node["TexID"], id);
        if (materials.contains(id))
            SPDLOG_WARN("Redefined material {} due to duplicate entry", (int)id);

        ReadValue(node["NormalStrength"], info.NormalStrength);
        ReadValue(node["SpecularStrength"], info.SpecularStrength);
        ReadValue(node["Metalness"], info.Metalness);
        ReadValue(node["Roughness"], info.Roughness);
        ReadValue(node["EmissiveStrength"], info.EmissiveStrength);
        ReadValue(node["LightReceived"], info.LightReceived);
        materials[id] = info;

        //auto& eclip = Resources::GetEffectClip(id);
        //for (auto frame : eclip.VClip.GetFrames()) {
        //    materials[frame] = info; // copy info to frames of the effect if present
        //}

        auto dclipId = Resources::GetDoorClipID(Resources::LookupLevelTexID(id));
        auto& dclip = Resources::GetDoorClip(dclipId);
        for (auto frame : dclip.GetFrames()) {
            materials[Resources::LookupTexID(frame)] = info; // copy info to frames of door
        }
    }

    ExtendedTextureInfo ExtendedTextureInfo::Load(const string& data) {
        ExtendedTextureInfo extendedInfo;
        try {
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(data));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                auto materialNode = root["Materials"];
                if (materialNode.valid() && !materialNode.is_seed()) {
                    for (const auto& node : materialNode.children()) {
                        ReadMaterialInfo(node, extendedInfo.Materials);
                    }
                }

                auto levelTextureNode = root["LevelTextures"];
                if (levelTextureNode.valid() && !levelTextureNode.is_seed()) {
                    for (const auto& node : levelTextureNode.children()) {
                        if (!node.valid() || node.is_seed()) continue;
                        auto info = ReadLightInfo(node);
                        if (extendedInfo.LevelTextures.contains(info.Id))
                            SPDLOG_WARN("Redefined texture {} due to duplicate entry", (int)info.Id);

                        extendedInfo.LevelTextures[info.Id] = info;
                    }
                }
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }

        SPDLOG_INFO("Loaded {} materials and {} light definitions", extendedInfo.Materials.size(), extendedInfo.LevelTextures.size());

        return extendedInfo;
    }
}

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

    Outrage::ProceduralInfo::Element ReadProceduralElement(ryml::ConstNodeRef node) {
        string json;
        node >> json;
        auto tree = ryml::parse_in_arena(ryml::to_csubstr(json));

        Outrage::ProceduralInfo::Element elem{};
        ReadValue(tree["Type"], elem.Type);
        ReadValue(tree["X1"], elem.X1);
        ReadValue(tree["Y1"], elem.Y1);
        ReadValue(tree["X2"], elem.X2);
        ReadValue(tree["Y2"], elem.Y2);
        ReadValue(tree["Frequency"], elem.Frequency);
        ReadValue(tree["Size"], elem.Size);
        ReadValue(tree["Speed"], elem.Speed);
        return elem;
    }

    void ReadWaterProcedural(ryml::NodeRef node, Outrage::ProceduralInfo& info) {
        ReadValue(node["Thickness"], info.Thickness);
        ReadValue(node["Light"], info.Light);
        ReadValue(node["OscillateTime"], info.OscillateTime);
        ReadValue(node["OscillateValue"], info.OscillateValue);

        for (const auto& ele : node["Elements"].children())
            info.Elements.push_back(ReadProceduralElement(ele));
    }

    void ReadFireProcedural(ryml::NodeRef node, Outrage::ProceduralInfo& info) {
        ReadValue(node["Heat"], info.Heat);

        string json;
        node["Palette"] >> json;
        auto palette = ryml::parse_in_arena(ryml::to_csubstr(json));
        int i = 0;
        for (const auto& ele : palette.rootref().children())
            ele >> info.Palette[i++];

        for (const auto& ele : node["Elements"].children())
            info.Elements.push_back(ReadProceduralElement(ele));
    }

    void ReadMaterialInfo(ryml::NodeRef node, span<MaterialInfo> materials) {
        if (!node.valid() || node.is_seed()) return;

        MaterialInfo info{};
        int texId;
        ReadValue(node["TexID"], texId);
        if (texId >= Resources::GameData.LevelTexIdx.size())
            return; // out of range

        info.ID = texId;

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

        auto procNode = node["Procedural"];
        if (!procNode.is_seed()) {
            Outrage::ProceduralInfo proc{};

            ReadValue(procNode["IsWater"], proc.IsWater);
            ReadValue(node["EvalTime"], proc.EvalTime);

            if (proc.IsWater)
                ReadWaterProcedural(procNode, proc);
            else
                ReadFireProcedural(procNode, proc);

            if (auto existing = GetProceduralInfo(TexID(texId))) {
                // todo: if IsWater changes between existing, recreate procedural
                // update existing
                *existing = proc;
            }
            else {
                // Insert new procedural
                Outrage::TextureInfo ti{};
                ti.Procedural = proc;
                SetFlag(ti.Flags, Outrage::TextureFlag::Procedural);
                if (proc.IsWater)
                    SetFlag(ti.Flags, Outrage::TextureFlag::WaterProcedural);

                AddProcedural(ti, TexID(texId));
            }
        }
    }

    void SaveProceduralElement(ryml::NodeRef node, const Outrage::ProceduralInfo::Element& elem, bool isFire) {
        ryml::Tree tree(1);
        tree.rootref() |= ryml::MAP;

        tree["Type"] << elem.Type;
        tree["X1"] << elem.X1;
        tree["Y1"] << elem.Y1;
        if (isFire && elem.FireType == Outrage::FireProceduralType::LineLightning) {
            tree["X2"] << elem.X2;
            tree["Y2"] << elem.Y2;
        }
        tree["Frequency"] << elem.Frequency;
        tree["Size"] << elem.Size;
        tree["Speed"] << elem.Speed;

        std::stringstream ss;
        ss << ryml::as_json(tree);
        node << ss.str();
    }

    void SaveFireProcedural(ryml::NodeRef node, const Outrage::ProceduralInfo& info) {
        node |= ryml::MAP;
        node["EvalTime"] << info.EvalTime;
        node["Heat"] << info.Heat;

        ryml::Tree tree(1);
        tree.rootref() |= ryml::SEQ;
        for (auto& x : info.Palette)
            tree.rootref().append_child() << x;

        std::stringstream ss;
        ss << ryml::as_json(tree);
        auto str = ss.str();
        node["Palette"] << str;

        auto elementsNode = node["Elements"];
        elementsNode |= ryml::SEQ;

        for (auto& elem : info.Elements) {
            auto child = elementsNode.append_child();
            SaveProceduralElement(child, elem, true);
        }
    }

    void SaveWaterProcedural(ryml::NodeRef node, const Outrage::ProceduralInfo& info) {
        node |= ryml::MAP;

        node["IsWater"] << true;
        node["EvalTime"] << info.EvalTime;
        node["Thickness"] << info.Thickness;
        node["Light"] << info.Light;
        node["OscillateTime"] << info.OscillateTime;
        node["OscillateValue"] << info.OscillateValue;

        auto elementsNode = node["Elements"];
        elementsNode |= ryml::SEQ;

        for (auto& elem : info.Elements) {
            auto child = elementsNode.append_child();
            SaveProceduralElement(child, elem, false);
        }
    }

    void SaveMaterialInfo(ryml::NodeRef node, TexID id, const MaterialInfo& info) {
        node |= ryml::MAP;
        node["TexID"] << (int)id;

        if (info.NormalStrength != 1)
            node["NormalStrength"] << info.NormalStrength;

        if (info.SpecularStrength != 1)
            node["SpecularStrength"] << info.SpecularStrength;

        if (info.Metalness != 0)
            node["Metalness"] << info.Metalness;

        node["Roughness"] << info.Roughness;

        if (info.EmissiveStrength > 0)
            node["EmissiveStrength"] << info.EmissiveStrength;

        if (info.LightReceived != 1)
            node["LightReceived"] << info.LightReceived;

        if (auto proc = GetProceduralInfo(id)) {
            auto procNode = node["Procedural"];

            if (proc->IsWater)
                SaveWaterProcedural(procNode, *proc);
            else
                SaveFireProcedural(procNode, *proc);
        }
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

    void LoadMaterialTable(const string& yaml, span<MaterialInfo> materials) {
        try {
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
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

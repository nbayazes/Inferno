#include "pch.h"
#include "MaterialInfo.h"
#include "Procedural.h"
#include "Resources.h"
#include "Yaml.h"
#include "Graphics/MaterialLibrary.h"

using namespace Yaml;

namespace Inferno {
    Outrage::ProceduralInfo::Element ReadProceduralElement(ryml::ConstNodeRef node) {
        string json;
        node >> json;
        auto tree = ryml::parse_in_arena(ryml::to_csubstr(json));

        Outrage::ProceduralInfo::Element elem{};
        ReadValue2(tree, "Type", elem.Type);
        ReadValue2(tree, "X1", elem.X1);
        ReadValue2(tree, "Y1", elem.Y1);
        ReadValue2(tree, "X2", elem.X2);
        ReadValue2(tree, "Y2", elem.Y2);
        ReadValue2(tree, "Frequency", elem.Frequency);
        ReadValue2(tree, "Size", elem.Size);
        ReadValue2(tree, "Speed", elem.Speed);
        return elem;
    }

    void ReadWaterProcedural(ryml::NodeRef node, Outrage::ProceduralInfo& info) {
        ReadValue2(node, "Thickness", info.Thickness);
        ReadValue2(node, "Light", info.Light);
        ReadValue2(node, "OscillateTime", info.OscillateTime);
        ReadValue2(node, "OscillateValue", info.OscillateValue);

        for (const auto& ele : node["Elements"].children())
            info.Elements.push_back(ReadProceduralElement(ele));
    }

    void ReadFireProcedural(ryml::NodeRef node, Outrage::ProceduralInfo& info) {
        ReadValue2(node, "Heat", info.Heat);

        string json;
        node["Palette"] >> json;
        auto palette = ryml::parse_in_arena(ryml::to_csubstr(json));
        int i = 0;
        for (const auto& ele : palette.rootref().children())
            ele >> info.Palette[i++];

        for (const auto& ele : node["Elements"].children())
            info.Elements.push_back(ReadProceduralElement(ele));
    }


    void SaveProceduralElement(ryml::NodeRef node, const Outrage::ProceduralInfo::Element& elem, bool isFire) {
        ryml::Tree tree(1);
        tree.rootref() |= ryml::MAP;

        tree["Type"] << elem.Type;
        tree["X1"] << elem.X1;
        tree["Y1"] << elem.Y1;
        if ((isFire && elem.FireType == Outrage::FireProceduralType::LineLightning) ||
            (!isFire && elem.WaterType == Outrage::WaterProceduralType::Line)) {
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
        node["IsWater"] << true;
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

    void SaveMaterialInfo(ryml::NodeRef node, const MaterialInfo& info) {
        node |= ryml::MAP;

        node["Name"] << info.Name;

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

        if (info.Flags != MaterialFlags::Default)
            node["Flags"] << (int)info.Flags;

        if (info.SpecularColor != Color(1, 1, 1, 1))
            node["SpecularColor"] << EncodeColor(info.SpecularColor);

        if (!info.Procedural.Elements.empty()) {
            auto procNode = node["Procedural"];
            procNode |= ryml::MAP;
            procNode["EvalTime"] << info.Procedural.EvalTime;

            if (info.Procedural.IsWater)
                SaveWaterProcedural(procNode, info.Procedural);
            else
                SaveFireProcedural(procNode, info.Procedural);
        }
    }

    Option<MaterialInfo> ReadMaterialInfo(ryml::NodeRef node) {
        if (!node.readable()) return {};

        MaterialInfo info{};
        string name;

        ReadValue2(node, "Name", info.Name);
        ReadValue2(node, "NormalStrength", info.NormalStrength);
        ReadValue2(node, "SpecularStrength", info.SpecularStrength);
        ReadValue2(node, "Metalness", info.Metalness);
        ReadValue2(node, "Roughness", info.Roughness);
        ReadValue2(node, "EmissiveStrength", info.EmissiveStrength);
        ReadValue2(node, "LightReceived", info.LightReceived);
        ReadValue2(node, "Flags", info.Flags);

        bool additive = false;
        ReadValue2(node, "Additive", additive);
        if (additive) SetFlag(info.Flags, MaterialFlags::Additive);

        ReadValue2(node, "SpecularColor", info.SpecularColor);

        auto procNode = node["Procedural"];
        if (!procNode.is_seed()) {
            ReadValue2(procNode, "IsWater", info.Procedural.IsWater);
            ReadValue2(procNode, "EvalTime", info.Procedural.EvalTime);

            if (info.Procedural.IsWater)
                ReadWaterProcedural(procNode, info.Procedural);
            else
                ReadFireProcedural(procNode, info.Procedural);
        }

        return info;
    }

    List<MaterialInfo> LoadMaterialTable(const string& yaml) {
        List<MaterialInfo> materials;
        materials.reserve(1000);

        try {
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                auto materialNode = root["Materials"];
                if (materialNode.readable()) {
                    for (const auto& node : materialNode.children()) {
                        try {
                            if (auto info = ReadMaterialInfo(node))
                                materials.push_back(*info);
                        }
                        catch (const Exception& e) {
                            SPDLOG_WARN("Error reading material info: {}", e.what());
                        }
                    }
                }
            }

            SPDLOG_INFO("Loaded {} material definitions", materials.size());
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading light info:\n{}", e.what());
        }

        return materials;
    }

    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials) {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            doc["Materials"] |= ryml::SEQ;

            for (auto& material : materials) {
                //if (material.ID == (int)TexID::None || material.ID == (int)Render::SHINY_FLAT_MATERIAL) continue;
                auto node = doc["Materials"].append_child();
                SaveMaterialInfo(node, material);
            }

            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
        }
    }
}

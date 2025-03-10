#include "pch.h"
#include "MaterialInfo.h"

#include "Procedural.h"
#include "Resources.h"
#include "Yaml.h"
#include "Graphics/MaterialLibrary.h"

using namespace Yaml;

namespace Inferno {
    MaterialInfo& MaterialInfoLibrary::GetMaterialInfo(TexID id) {
        if (!Seq::inRange(_materialInfo, (int)id)) return _defaultMaterialInfo;
        return _materialInfo[(int)id];
    }

    MaterialInfo& MaterialInfoLibrary::GetMaterialInfo(LevelTexID id) {
        return GetMaterialInfo(Resources::LookupTexID(id));
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

    void SaveMaterialInfo(ryml::NodeRef node, TexID id, const MaterialInfo& info) {
        node |= ryml::MAP;

        auto& ti = Resources::GetTextureInfo(id);
        ASSERT(!ti.Name.empty());
        node["Name"] << ti.Name;

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

        if (info.Additive != 0)
            node["Additive"] << info.Additive;

        if (info.SpecularColor != Color(1, 1, 1, 1))
            node["SpecularColor"] << EncodeColor(info.SpecularColor);

        if (auto proc = GetProcedural(id)) {
            auto procNode = node["Procedural"];
            procNode |= ryml::MAP;

            auto& procInfo = proc->Info.Procedural;

            procNode["EvalTime"] << procInfo.EvalTime;
            if (!procInfo.Wrap) procNode["Wrap"] << procInfo.Wrap;

            if (proc->Info.Procedural.IsWater)
                SaveWaterProcedural(procNode, procInfo);
            else
                SaveFireProcedural(procNode, procInfo);
        }
    }


    void ReadMaterialInfo(ryml::NodeRef node, span<MaterialInfo> materials) {
        if (!node.readable()) return;

        auto texId = TexID::None;

        MaterialInfo info{};
        string name;
        if (ReadValue(node["Name"], name))
            texId = Resources::FindTexture(name);

        // Check for old texid property
        if (texId == TexID::None)
            ReadValue(node["TexID"], texId);

        if (texId == TexID::None)
            return; // couldn't find texture from name or id

        info.ID = (int)texId;

        ReadValue(node["NormalStrength"], info.NormalStrength);
        ReadValue(node["SpecularStrength"], info.SpecularStrength);
        ReadValue(node["Metalness"], info.Metalness);
        ReadValue(node["Roughness"], info.Roughness);
        ReadValue(node["EmissiveStrength"], info.EmissiveStrength);
        ReadValue(node["LightReceived"], info.LightReceived);
        ReadValue(node["Additive"], info.Additive);
        ReadValue(node["SpecularColor"], info.SpecularColor);

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
            ReadValue(procNode["EvalTime"], proc.EvalTime);
            ReadValue(procNode["Wrap"], proc.Wrap);

            if (proc.IsWater)
                ReadWaterProcedural(procNode, proc);
            else
                ReadFireProcedural(procNode, proc);

            if (auto existing = GetProcedural(TexID(texId))) {
                // todo: if IsWater changes between existing, recreate procedural
                // update existing
                existing->Info.Procedural = proc;
            }
            else {
                // Insert new procedural
                Outrage::TextureInfo ti{};
                ti.Procedural = proc;
                ti.Name = Resources::GetTextureInfo(TexID(texId)).Name;
                SetFlag(ti.Flags, Outrage::TextureFlag::Procedural);
                if (proc.IsWater)
                    SetFlag(ti.Flags, Outrage::TextureFlag::WaterProcedural);

                AddProcedural(ti, TexID(texId));
            }
        }
    }

    void LoadMaterialTable(const string& yaml, span<MaterialInfo> materials) {
        try {
            if (materials.empty()) return;

            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
            ryml::NodeRef root = doc.rootref();
            int count = 0;

            if (root.is_map()) {
                auto materialNode = root["Materials"];
                if (materialNode.readable()) {
                    for (const auto& node : materialNode.children()) {
                        ReadMaterialInfo(node, materials);
                        count++;
                    }
                }
            }

            // Hard code special flat material
            if (materials.size() >= (int)Render::SHINY_FLAT_MATERIAL) {
                auto& flat = materials[(int)Render::SHINY_FLAT_MATERIAL];
                flat.ID = (int)Render::SHINY_FLAT_MATERIAL;
                flat.Metalness = 1.0f;
                flat.Roughness = 0.375f;
                flat.LightReceived = 0.5f;
                flat.SpecularStrength = 0.8f;
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
            // skip material 0 as it is a placeholder
            for (int i = 1; i < materials.size(); i++) {
                if (materials[i].ID == (int)TexID::None || materials[i].ID == (int)Render::SHINY_FLAT_MATERIAL) continue;
                auto node = doc["Materials"].append_child();
                SaveMaterialInfo(node, (TexID)i, materials[i]);
            }

            stream << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving level metadata:\n{}", e.what());
        }
    }
}

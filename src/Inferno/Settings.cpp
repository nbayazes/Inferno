#include "pch.h"
#define MAGIC_ENUM_RANGE_MIN 0
#define MAGIC_ENUM_RANGE_MAX 256
#include <magic_enum.hpp>

#include <fstream>
#include "Settings.h"
#include <spdlog/spdlog.h>
#include "Yaml.h"
#include "Editor/Bindings.h"

using namespace Yaml;

namespace Inferno {
    void EditorSettings::AddRecentFile(filesystem::path path) {
        if (!filesystem::exists(path)) return;

        if (Seq::contains(RecentFiles, path))
            Seq::remove(RecentFiles, path);

        RecentFiles.push_front(path);

        while (RecentFiles.size() > MaxRecentFiles)
            RecentFiles.pop_back();
    }

    void SaveGraphicsSettings(ryml::NodeRef node, const GraphicsSettings& s) {
        node |= ryml::MAP;
        node["HighRes"] << s.HighRes;
        node["EnableBloom"] << s.EnableBloom;
        node["MsaaSamples"] << s.MsaaSamples;
        node["ForegroundFpsLimit"] << s.ForegroundFpsLimit;
        node["BackgroundFpsLimit"] << s.BackgroundFpsLimit;
    }

    GraphicsSettings LoadGraphicsSettings(ryml::NodeRef node) {
        GraphicsSettings s{};
        if (node.is_seed()) return s;
        ReadValue(node["HighRes"], s.HighRes);
        ReadValue(node["EnableBloom"], s.EnableBloom);
        ReadValue(node["MsaaSamples"], s.MsaaSamples);
        if (s.MsaaSamples != 1 && s.MsaaSamples != 2 && s.MsaaSamples != 4 && s.MsaaSamples != 8)
            s.MsaaSamples = 1;

        ReadValue(node["ForegroundFpsLimit"], s.ForegroundFpsLimit);
        ReadValue(node["BackgroundFpsLimit"], s.BackgroundFpsLimit);
        return s;
    }

    void SaveOpenWindows(ryml::NodeRef node, const EditorSettings::OpenWindows& w) {
        node |= ryml::MAP;
        node["Lighting"] << w.Lighting;
        node["Properties"] << w.Properties;
        node["Textures"] << w.Textures;
        node["Reactor"] << w.Reactor;
        node["Diagnostics"] << w.Diagnostics;
        node["Noise"] << w.Noise;
        node["TunnelBuilder"] << w.TunnelBuilder;
        node["Sound"] << w.Sound;
        node["BriefingEditor"] << w.BriefingEditor;
    }

    EditorSettings::OpenWindows LoadOpenWindows(ryml::NodeRef node) {
        EditorSettings::OpenWindows w{};
        if (node.is_seed()) return w;
        ReadValue(node["Lighting"], w.Lighting);
        ReadValue(node["Properties"], w.Properties);
        ReadValue(node["Textures"], w.Textures);
        ReadValue(node["Reactor"], w.Reactor);
        ReadValue(node["Diagnostics"], w.Diagnostics);
        ReadValue(node["Noise"], w.Noise);
        ReadValue(node["TunnelBuilder"], w.TunnelBuilder);
        ReadValue(node["Sound"], w.Sound);
        ReadValue(node["BriefingEditor"], w.BriefingEditor);
        return w;
    }

    void SaveSelectionSettings(ryml::NodeRef node, const EditorSettings::SelectionSettings& s) {
        node |= ryml::MAP;
        node["PlanarTolerance"] << s.PlanarTolerance;
        node["StopAtWalls"] << s.StopAtWalls;
        node["UseTMap1"] << s.UseTMap1;
        node["UseTMap2"] << s.UseTMap2;
    }

    EditorSettings::SelectionSettings LoadSelectionSettings(ryml::NodeRef node) {
        EditorSettings::SelectionSettings s{};
        if (node.is_seed()) return s;
        ReadValue(node["PlanarTolerance"], s.PlanarTolerance);
        ReadValue(node["StopAtWalls"], s.StopAtWalls);
        ReadValue(node["UseTMap1"], s.UseTMap1);
        ReadValue(node["UseTMap2"], s.UseTMap2);
        return s;
    }

    void SaveLightSettings(ryml::NodeRef node, const LightSettings& s) {
        node |= ryml::MAP;
        node["Ambient"] << EncodeColor(s.Ambient);
        node["AccurateVolumes"] << s.AccurateVolumes;
        node["Bounces"] << s.Bounces;
        node["DistanceThreshold"] << s.DistanceThreshold;
        node["EnableColor"] << s.EnableColor;
        node["EnableOcclusion"] << s.EnableOcclusion;
        node["Falloff"] << s.Falloff;
        node["MaxValue"] << s.MaxValue;
        node["Multiplier"] << s.Multiplier;
        node["Radius"] << s.Radius;
        node["Reflectance"] << s.Reflectance;
    }

    LightSettings LoadLightSettings(ryml::NodeRef node) {
        LightSettings settings{};
        if (node.is_seed()) return settings;

        ReadValue(node["Ambient"], settings.Ambient);
        ReadValue(node["AccurateVolumes"], settings.AccurateVolumes);
        ReadValue(node["Bounces"], settings.Bounces);
        ReadValue(node["DistanceThreshold"], settings.DistanceThreshold);
        ReadValue(node["EnableColor"], settings.EnableColor);
        ReadValue(node["EnableOcclusion"], settings.EnableOcclusion);
        ReadValue(node["Falloff"], settings.Falloff);
        ReadValue(node["MaxValue"], settings.MaxValue);
        ReadValue(node["Multiplier"], settings.Multiplier);
        ReadValue(node["Radius"], settings.Radius);
        ReadValue(node["Reflectance"], settings.Reflectance);
        return settings;
    }

    void SaveEditorBindings(ryml::NodeRef node) {
        node |= ryml::SEQ;

        for (auto& binding : Editor::Bindings::Active.GetBindings()) {
            auto child = node.append_child();
            child |= ryml::MAP;
            auto action = magic_enum::enum_name(binding.Action);
            auto key = string(magic_enum::enum_name(binding.Key));
            if (binding.Alt) key = "Alt " + key;
            if (binding.Shift) key = "Shift " + key;
            if (binding.Control) key = "Ctrl " + key;
            child[ryml::to_csubstr(action.data())] << key;
        }
    }

    void LoadEditorBindings(ryml::NodeRef node) {
        if (node.is_seed()) return;
        auto& bindings = Editor::Bindings::Active;
        bindings.Clear(); // we have some bindings to replace defaults!

        for (const auto& c : node.children()) {
            if (c.is_seed() || !c.is_map()) continue;

            auto kvp = c.child(0);
            string value, command;
            if (kvp.has_key()) command = string(kvp.key().data(), kvp.key().len);
            if (kvp.has_val()) value = string(kvp.val().data(), kvp.val().len);
            if (value.empty() || command.empty()) continue;

            Editor::EditorBinding binding{};
            if (auto commandName = magic_enum::enum_cast<Editor::EditorAction>(command))
                binding.Action = *commandName;

            auto tokens = String::Split(value, ' ');
            binding.Alt = Seq::contains(tokens, "Alt");
            binding.Shift = Seq::contains(tokens, "Shift");
            binding.Control = Seq::contains(tokens, "Ctrl");
            if (auto key = magic_enum::enum_cast<DirectX::Keyboard::Keys>(tokens.back()))
                binding.Key = *key;

            // Note that it is valid for Key to equal None to indicate that the user unbound it on purpose
            bindings.Add(binding);

            if (binding.Action == Editor::EditorAction::HoldMouselook)
                Editor::Bindings::MouselookHoldBinding = binding;
        }

        // copy bindings before adding defaults so that multiple shortcuts for the same action will apply properly
        Editor::EditorBindings fileBindings = bindings;

        for (auto& defaultBinding : Editor::Bindings::Default.GetBindings()) {
            if (!fileBindings.GetBinding(defaultBinding.Action)) {
                // there's a default binding for this action and the file didn't provide one
                bindings.Add(defaultBinding);
            }
        }
    }

    void SaveBindings(ryml::NodeRef node) {
        node |= ryml::MAP;
        SaveEditorBindings(node["Editor"]);

        // todo: Game bindings
    }

    void SaveEditorSettings(ryml::NodeRef node, const EditorSettings& s) {
        node |= ryml::MAP;
        WriteSequence(node["RecentFiles"], s.RecentFiles);

        node["EnableWallMode"] << s.EnableWallMode;
        node["EnableTextureMode"] << s.EnableTextureMode;
        node["ObjectRenderDistance"] << s.ObjectRenderDistance;

        node["TranslationSnap"] << s.TranslationSnap;
        node["RotationSnap"] << s.RotationSnap;

        node["MouselookSensitivity"] << s.MouselookSensitivity;
        node["MoveSpeed"] << s.MoveSpeed;

        node["SelectionMode"] << (int)s.SelectionMode;
        node["InsertMode"] << (int)s.InsertMode;

        node["ShowObjects"] << s.ShowObjects;
        node["ShowWalls"] << s.ShowWalls;
        node["ShowTriggers"] << s.ShowTriggers;
        node["ShowFlickeringLights"] << s.ShowFlickeringLights;
        node["ShowAnimation"] << s.ShowAnimation;
        node["ShowMatcenEffects"] << s.ShowMatcenEffects;
        node["WireframeOpacity"] << s.WireframeOpacity;

        node["ShowWireframe"] << s.ShowWireframe;
        node["RenderMode"] << (int)s.RenderMode;
        node["GizmoSize"] << s.GizmoSize;
        node["InvertY"] << s.InvertY;
        node["FieldOfView"] << s.FieldOfView;
        node["FontSize"] << s.FontSize;

        node["EditBothWallSides"] << s.EditBothWallSides;
        node["ReopenLastLevel"] << s.ReopenLastLevel;
        node["SelectMarkedSegment"] << s.SelectMarkedSegment;
        node["ResetUVsOnAlign"] << s.ResetUVsOnAlign;
        node["WeldTolerance"] << s.WeldTolerance;

        node["Undos"] << s.UndoLevels;
        node["AutosaveMinutes"] << s.AutosaveMinutes;
        node["CoordinateSystem"] << (int)s.CoordinateSystem;
        node["EnablePhysics"] << s.EnablePhysics;
        node["TexturePreviewSize"] << (int)s.TexturePreviewSize;
        node["ShowLevelTitle"] << s.ShowLevelTitle;

        SaveSelectionSettings(node["Selection"], s.Selection);
        SaveOpenWindows(node["Windows"], s.Windows);
        SaveLightSettings(node["Lighting"], s.Lighting);
    }

    EditorSettings LoadEditorSettings(ryml::NodeRef node, InfernoSettings& settings) {
        EditorSettings s{};
        if (node.is_seed()) return s;

        for (const auto& c : node["RecentFiles"].children()) {
            filesystem::path path;
            ReadValue(c, path);
            if (!path.empty()) s.RecentFiles.push_back(path);
        }

        // Legacy. Read editor data paths into top level data paths
        auto dataPaths = node["DataPaths"];
        if (!dataPaths.is_seed()) {
            for (const auto& c : node["DataPaths"].children()) {
                filesystem::path path;
                ReadValue(c, path);
                if (!path.empty()) settings.DataPaths.push_back(path);
            }
        }

        ReadValue(node["EnableWallMode"], s.EnableWallMode);
        ReadValue(node["EnableTextureMode"], s.EnableTextureMode);
        ReadValue(node["ObjectRenderDistance"], s.ObjectRenderDistance);

        ReadValue(node["TranslationSnap"], s.TranslationSnap);
        ReadValue(node["RotationSnap"], s.RotationSnap);

        ReadValue(node["MouselookSensitivity"], s.MouselookSensitivity);
        ReadValue(node["MoveSpeed"], s.MoveSpeed);

        ReadValue(node["SelectionMode"], (int&)s.SelectionMode);
        ReadValue(node["InsertMode"], (int&)s.InsertMode);

        ReadValue(node["ShowObjects"], s.ShowObjects);
        ReadValue(node["ShowWalls"], s.ShowWalls);
        ReadValue(node["ShowTriggers"], s.ShowTriggers);
        ReadValue(node["ShowFlickeringLights"], s.ShowFlickeringLights);
        ReadValue(node["ShowAnimation"], s.ShowAnimation);
        ReadValue(node["ShowMatcenEffects"], s.ShowMatcenEffects);
        ReadValue(node["WireframeOpacity"], s.WireframeOpacity);

        ReadValue(node["ShowWireframe"], s.ShowWireframe);
        ReadValue(node["RenderMode"], (int&)s.RenderMode);
        ReadValue(node["GizmoSize"], s.GizmoSize);
        ReadValue(node["InvertY"], s.InvertY);
        ReadValue(node["FieldOfView"], s.FieldOfView);
        s.FieldOfView = std::clamp(s.FieldOfView, 45.0f, 130.0f);
        ReadValue(node["FontSize"], s.FontSize);
        s.FontSize = std::clamp(s.FontSize, 8, 48);

        ReadValue(node["EditBothWallSides"], s.EditBothWallSides);
        ReadValue(node["ReopenLastLevel"], s.ReopenLastLevel);
        ReadValue(node["SelectMarkedSegment"], s.SelectMarkedSegment);
        ReadValue(node["ResetUVsOnAlign"], s.ResetUVsOnAlign);
        ReadValue(node["WeldTolerance"], s.WeldTolerance);

        ReadValue(node["Undos"], s.UndoLevels);
        ReadValue(node["AutosaveMinutes"], s.AutosaveMinutes);
        ReadValue(node["CoordinateSystem"], (int&)s.CoordinateSystem);
        ReadValue(node["EnablePhysics"], (int&)s.EnablePhysics);
        ReadValue(node["TexturePreviewSize"], (int&)s.TexturePreviewSize);
        ReadValue(node["ShowLevelTitle"], s.ShowLevelTitle);

        s.Selection = LoadSelectionSettings(node["Selection"]);
        s.Windows = LoadOpenWindows(node["Windows"]);
        s.Lighting = LoadLightSettings(node["Lighting"]);
        return s;
    }

    void Settings::Save(filesystem::path path) {
        try {
            ryml::Tree doc(128, 128);
            doc.rootref() |= ryml::MAP;

            doc["Descent1Path"] << Settings::Inferno.Descent1Path.string();
            doc["Descent2Path"] << Settings::Inferno.Descent2Path.string();
            WriteSequence(doc["DataPaths"], Settings::Inferno.DataPaths);
            SaveEditorSettings(doc["Editor"], Settings::Editor);
            SaveGraphicsSettings(doc["Render"], Settings::Graphics);
            SaveBindings(doc["Bindings"]);

            std::ofstream file(path);
            file << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving config file:\n{}", e.what());
        }
    }

    void Settings::Load(filesystem::path path) {
        try {
            std::ifstream file(path);
            if (!file) return;

            std::stringstream buffer;
            buffer << file.rdbuf();
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(buffer.str()));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                ReadValue(root["Descent1Path"], Settings::Inferno.Descent1Path);
                ReadValue(root["Descent2Path"], Settings::Inferno.Descent2Path);
                auto dataPaths = root["DataPaths"];
                if (!dataPaths.is_seed()) {
                    for (const auto& c : root["DataPaths"].children()) {
                        filesystem::path dataPath;
                        ReadValue(c, dataPath);
                        if (!dataPath.empty()) Settings::Inferno.DataPaths.push_back(dataPath);
                    }
                }

                Settings::Editor = LoadEditorSettings(root["Editor"], Settings::Inferno);
                Settings::Graphics = LoadGraphicsSettings(root["Render"]);
                auto bindings = root["Bindings"];
                if (!bindings.is_seed()) {
                    LoadEditorBindings(bindings["Editor"]);
                }
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading config file:\n{}", e.what());
        }
    }
}


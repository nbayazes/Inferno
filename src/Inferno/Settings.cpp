#include "pch.h"
#include <fstream>
#include "Settings.h"
#include <spdlog/spdlog.h>
#include "Yaml.h"

using namespace Yaml;

namespace Inferno::Settings {
    constexpr auto SettingsFile = "inferno.cfg";


    void AddRecentFile(filesystem::path path) {
        if (!filesystem::exists(path)) return;

        if (Seq::contains(RecentFiles, path))
            Seq::remove(RecentFiles, path);

        RecentFiles.push_front(path);

        while (RecentFiles.size() > MaxRecentFiles)
            RecentFiles.pop_back();
    }

    void SaveRenderSettings(ryml::NodeRef node) {
        node |= ryml::MAP;
        node["HighRes"] << HighRes;
        node["EnableBloom"] << EnableBloom;
        node["MsaaSamples"] << MsaaSamples;
        node["ForegroundFpsLimit"] << ForegroundFpsLimit;
        node["BackgroundFpsLimit"] << BackgroundFpsLimit;
    }

    void LoadRenderSettings(ryml::NodeRef node) {
        if (node.is_seed()) return;
        ReadValue(node["HighRes"], HighRes);
        ReadValue(node["EnableBloom"], EnableBloom);
        ReadValue(node["MsaaSamples"], MsaaSamples);
        if (MsaaSamples != 1 && MsaaSamples != 2 && MsaaSamples != 4 && MsaaSamples != 8)
            MsaaSamples = 1;

        ReadValue(node["ForegroundFpsLimit"], ForegroundFpsLimit);
        ReadValue(node["BackgroundFpsLimit"], BackgroundFpsLimit);
    }

    void SaveOpenWindows(ryml::NodeRef node) {
        node |= ryml::MAP;
        node["Lighting"] << Windows.Lighting;
        node["Properties"] << Windows.Properties;
        node["Textures"] << Windows.Textures;
        node["Reactor"] << Windows.Reactor;
        node["Diagnostics"] << Windows.Diagnostics;
        node["Noise"] << Windows.Noise;
        node["TunnelBuilder"] << Windows.TunnelBuilder;
        node["Sound"] << Windows.Sound;
        node["BriefingEditor"] << Windows.BriefingEditor;
    }

    void LoadOpenWindows(ryml::NodeRef node) {
        if (node.is_seed()) return;
        ReadValue(node["Lighting"], Windows.Lighting);
        ReadValue(node["Properties"], Windows.Properties);
        ReadValue(node["Textures"], Windows.Textures);
        ReadValue(node["Reactor"], Windows.Reactor);
        ReadValue(node["Diagnostics"], Windows.Diagnostics);
        ReadValue(node["Noise"], Windows.Noise);
        ReadValue(node["TunnelBuilder"], Windows.TunnelBuilder);
        ReadValue(node["Sound"], Windows.Sound);
        ReadValue(node["BriefingEditor"], Windows.BriefingEditor);
    }

    void SaveSelectionSettings(ryml::NodeRef node) {
        node |= ryml::MAP;
        node["PlanarTolerance"] << Selection.PlanarTolerance;
        node["StopAtWalls"] << Selection.StopAtWalls;
        node["UseTMap1"] << Selection.UseTMap1;
        node["UseTMap2"] << Selection.UseTMap2;
    }

    void LoadSelectionSettings(ryml::NodeRef node) {
        if (node.is_seed()) return;
        ReadValue(node["PlanarTolerance"], Selection.PlanarTolerance);
        ReadValue(node["StopAtWalls"], Selection.StopAtWalls);
        ReadValue(node["UseTMap1"], Selection.UseTMap1);
        ReadValue(node["UseTMap2"], Selection.UseTMap2);
    }

    void SaveLightSettings(ryml::NodeRef node) {
        node |= ryml::MAP;
        node["Ambient"] << EncodeColor(Lighting.Ambient);
        node["AccurateVolumes"] << Lighting.AccurateVolumes;
        node["Bounces"] << Lighting.Bounces;
        node["DistanceThreshold"] << Lighting.DistanceThreshold;
        node["EnableColor"] << Lighting.EnableColor;
        node["EnableOcclusion"] << Lighting.EnableOcclusion;
        node["Falloff"] << Lighting.Falloff;
        node["MaxValue"] << Lighting.MaxValue;
        node["Multiplier"] << Lighting.Multiplier;
        node["Radius"] << Lighting.Radius;
        node["Reflectance"] << Lighting.Reflectance;
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

    void SaveEditorSettings(ryml::NodeRef node) {
        node |= ryml::MAP;
        WriteSequence(node["RecentFiles"], RecentFiles);
        WriteSequence(node["DataPaths"], DataPaths);

        node["EnableWallMode"] << EnableWallMode;
        node["EnableTextureMode"] << EnableTextureMode;
        node["ObjectRenderDistance"] << ObjectRenderDistance;

        node["TranslationSnap"] << TranslationSnap;
        node["RotationSnap"] << RotationSnap;

        node["MouselookSensitivity"] << MouselookSensitivity;
        node["MoveSpeed"] << MoveSpeed;

        node["SelectionMode"] << (int)SelectionMode;
        node["InsertMode"] << (int)InsertMode;

        node["ShowObjects"] << ShowObjects;
        node["ShowWalls"] << ShowWalls;
        node["ShowTriggers"] << ShowTriggers;
        node["ShowFlickeringLights"] << ShowFlickeringLights;
        node["ShowAnimation"] << ShowAnimation;
        node["ShowMatcenEffects"] << ShowMatcenEffects;
        node["WireframeOpacity"] << WireframeOpacity;

        node["ShowWireframe"] << ShowWireframe;
        node["RenderMode"] << (int)RenderMode;
        node["GizmoSize"] << GizmoSize;
        node["InvertY"] << InvertY;
        node["FieldOfView"] << FieldOfView;
        node["FontSize"] << FontSize;

        node["EditBothWallSides"] << EditBothWallSides;
        node["ReopenLastLevel"] << ReopenLastLevel;
        node["SelectMarkedSegment"] << SelectMarkedSegment;
        node["ResetUVsOnAlign"] << ResetUVsOnAlign;
        node["WeldTolerance"] << WeldTolerance;

        node["Undos"] << UndoLevels;
        node["AutosaveMinutes"] << AutosaveMinutes;
        node["CoordinateSystem"] << (int)CoordinateSystem;
        node["EnablePhysics"] << EnablePhysics;
        node["TexturePreviewSize"] << (int)TexturePreviewSize;

        SaveSelectionSettings(node["Selection"]);
        SaveOpenWindows(node["Windows"]);
        SaveLightSettings(node["Lighting"]);
    }

    void LoadEditorSettings(ryml::NodeRef node) {
        if (node.is_seed()) return;

        for (const auto& c : node["RecentFiles"].children()) {
            filesystem::path path;
            ReadValue(c, path);
            if (!path.empty()) RecentFiles.push_back(path);
        }

        for (const auto& c : node["DataPaths"].children()) {
            filesystem::path path;
            ReadValue(c, path);
            if (!path.empty()) DataPaths.push_back(path);
        }

        ReadValue(node["EnableWallMode"], EnableWallMode);
        ReadValue(node["EnableTextureMode"], EnableTextureMode);
        ReadValue(node["ObjectRenderDistance"], ObjectRenderDistance);

        ReadValue(node["TranslationSnap"], TranslationSnap);
        ReadValue(node["RotationSnap"], RotationSnap);

        ReadValue(node["MouselookSensitivity"], MouselookSensitivity);
        ReadValue(node["MoveSpeed"], MoveSpeed);

        ReadValue(node["SelectionMode"], (int&)SelectionMode);
        ReadValue(node["InsertMode"], (int&)InsertMode);

        ReadValue(node["ShowObjects"], ShowObjects);
        ReadValue(node["ShowWalls"], ShowWalls);
        ReadValue(node["ShowTriggers"], ShowTriggers);
        ReadValue(node["ShowFlickeringLights"], ShowFlickeringLights);
        ReadValue(node["ShowAnimation"], ShowAnimation);
        ReadValue(node["ShowMatcenEffects"], ShowMatcenEffects);
        ReadValue(node["WireframeOpacity"], WireframeOpacity);

        ReadValue(node["ShowWireframe"], ShowWireframe);
        ReadValue(node["RenderMode"], (int&)RenderMode);
        ReadValue(node["GizmoSize"], GizmoSize);
        ReadValue(node["InvertY"], InvertY);
        ReadValue(node["FieldOfView"], FieldOfView);
        FieldOfView = std::clamp(FieldOfView, 45.0f, 130.0f);
        ReadValue(node["FontSize"], FontSize);
        FontSize = std::clamp(FontSize, 8, 48);

        ReadValue(node["EditBothWallSides"], EditBothWallSides);
        ReadValue(node["ReopenLastLevel"], ReopenLastLevel);
        ReadValue(node["SelectMarkedSegment"], SelectMarkedSegment);
        ReadValue(node["ResetUVsOnAlign"], ResetUVsOnAlign);
        ReadValue(node["WeldTolerance"], WeldTolerance);

        ReadValue(node["Undos"], UndoLevels);
        ReadValue(node["AutosaveMinutes"], AutosaveMinutes);
        ReadValue(node["CoordinateSystem"], (int&)CoordinateSystem);
        ReadValue(node["EnablePhysics"], (int&)EnablePhysics);
        ReadValue(node["TexturePreviewSize"], (int&)TexturePreviewSize);

        LoadSelectionSettings(node["Selection"]);
        LoadOpenWindows(node["Windows"]);
        Lighting = LoadLightSettings(node["Lighting"]);
    }

    void Save() {
        try {
            ryml::Tree doc(30, 128);
            doc.rootref() |= ryml::MAP;

            doc["Descent1Path"] << Descent1Path.string();
            doc["Descent2Path"] << Descent2Path.string();

            SaveEditorSettings(doc["Editor"]);
            SaveRenderSettings(doc["Render"]);

            std::ofstream file(SettingsFile);
            file << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving config file:\n{}", e.what());
        }
    }

    void Load() {
        try {
            std::ifstream file(SettingsFile);
            if (!SettingsFile) return;

            std::stringstream buffer;
            buffer << file.rdbuf();
            ryml::Tree doc = ryml::parse(ryml::to_csubstr(buffer.str()));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                ReadValue(root["Descent1Path"], Descent1Path);
                ReadValue(root["Descent2Path"], Descent2Path);

                LoadEditorSettings(root["Editor"]);
                LoadRenderSettings(root["Render"]);
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading config file:\n{}", e.what());
        }
    }
}


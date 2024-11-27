#include "pch.h"

#define MAGIC_ENUM_RANGE_MIN 0
#define MAGIC_ENUM_RANGE_MAX 256

#include <Settings.h>
#include <fstream>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include "Game.Bindings.h"
#include "Game.h"
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
        node["EnableForegroundFpsLimit"] << s.EnableForegroundFpsLimit;
        node["ForegroundFpsLimit"] << s.ForegroundFpsLimit;
        node["BackgroundFpsLimit"] << s.BackgroundFpsLimit;
        node["UseVsync"] << s.UseVsync;
        node["FilterMode"] << (int)s.FilterMode;
    }

    GraphicsSettings LoadGraphicsSettings(ryml::NodeRef node) {
        GraphicsSettings s{};
        if (node.is_seed()) return s;
        ReadValue(node["HighRes"], s.HighRes);
        ReadValue(node["EnableBloom"], s.EnableBloom);
        ReadValue(node["MsaaSamples"], s.MsaaSamples);
        if (s.MsaaSamples != 1 && s.MsaaSamples != 2 && s.MsaaSamples != 4 && s.MsaaSamples != 8)
            s.MsaaSamples = 1;

        ReadValue(node["EnableForegroundFpsLimit"], s.EnableForegroundFpsLimit);
        ReadValue(node["ForegroundFpsLimit"], s.ForegroundFpsLimit);
        ReadValue(node["BackgroundFpsLimit"], s.BackgroundFpsLimit);
        ReadValue(node["UseVsync"], s.UseVsync);
        ReadValue(node["FilterMode"], (int&)s.FilterMode);

        s.ForegroundFpsLimit = std::max(s.ForegroundFpsLimit, 20);
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
        node["TextureEditor"] << w.TextureEditor;
        node["MaterialEditor"] << w.MaterialEditor;
        node["TerrainEditor"] << w.TerrainEditor;
        node["Scale"] << w.Scale;
        node["Debug"] << w.Debug;
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
        ReadValue(node["TextureEditor"], w.TextureEditor);
        ReadValue(node["MaterialEditor"], w.MaterialEditor);
        ReadValue(node["TerrainEditor"], w.TerrainEditor);
        ReadValue(node["Scale"], w.Scale);
        ReadValue(node["Debug"], w.Debug);
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
        node["Ambient"] << EncodeColor3(s.Ambient);
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
        node["Multithread"] << s.Multithread;
    }

    void SavePalette(ryml::NodeRef node, const Array<Color, PALETTE_SIZE>& palette) {
        node |= ryml::SEQ;
        for (auto& color : palette) {
            node.append_child() << EncodeColor3(color);
        }
    }

    Array<Color, PALETTE_SIZE> GetDefaultPalette() {
        Array<Color, PALETTE_SIZE> palette{};
        int i = 0;

        auto addColor = [&i, &palette](const Color& color) {
            if (i >= palette.size()) return;
            palette[i++] = color;
        };

        addColor({ 0.625, 0.75, 1 });
        addColor({ 0.758, 0.758, 1 });
        addColor({ 0.848, 0.906, 1 });
        addColor({ 1, 1, 1 });
        addColor({ 0.75, 1, 1 });
        addColor({ 0.75, 1, 1 });
        addColor({ 1, 0.5, 0.5 });
        addColor({ 1, 0.231, 0.231 });
        addColor({ 1, 0.125, 0.125 });
        addColor({ 0.6, 0.15, 0 });
        addColor({ 1.25, 0.25, 0 });
        addColor({ 1.25, 0.75, 0.25 });
        addColor({ 1, 0.5, 0.125 });
        addColor({ 1, 0.75, 0.5 });
        addColor({ 1, 0.727, 0.364 });
        addColor({ 1, 0.75, 0.5 });
        addColor({ 1, 1, 0.75 });
        addColor({ 1, 1, 0.25 });
        addColor({ 0.5, 1, 0.75 });
        addColor({ 0.0667, 1, 0.967 });
        addColor({ 0.5, 1, 0.3 });
        addColor({ 0.125, 1, 0.5 });
        addColor({ 0.333, 1, 0.667 });
        addColor({ 0.5, 1, 0.5 });
        addColor({ 0.181, 0.435, 1 });
        addColor({ 0.125, 0.375, 1 });
        addColor({ 0.25, 0.5, 1 });
        addColor({ 0.278, 0.278, 1 });
        addColor({ 0, 0, 0 });
        addColor({ 0, 0, 0 });
        addColor({ 1, 0.3, 0.6 });
        addColor({ 0.75, 0.125, 1 });
        addColor({ 0.929, 0, 1 });
        addColor({ 0.5, 0.5, 1 });
        addColor({ 0.7, 0.6, 1 });
        addColor({ 0.893, 0.781, 1 });

        // Load defaults (D3 light colors)
        //addColor({ .25f, .3f, .4f }); // bluish
        //addColor({ .5f, .5f, .66f }); // blue lamp
        //addColor({ .47f, .50f, .55f }); // white lamp
        //addColor({ .3f, .3f, .3f }); // white
        //addColor({ .3f, .4f, .4f }); // rusty teal
        //addColor({ .12f, .16f, .16f }); // strip teal

        //addColor({ .4f, .2f, .2f }); // reddish
        //addColor({ 1.3f, .3f, .3f }); // super red
        //addColor({ .4f, .05f, .05f }); // red
        //addColor({ .24f, .06f, 0 }); // strip red
        //addColor({ .5f, .1f, 0 }); // bright orange
        //addColor({ .5f, .3f, .1f }); // bright orange

        //addColor({ .4f, .2f, .05f }); // orange
        //addColor({ .4f, .3f, .2f }); // orangish
        //addColor({ .44f, .32f, .16f }); // bright orange
        //addColor({ .2f, .15f, .1f }); // strip orange
        //addColor({ .44f, .44f, .33f }); // bright yellow
        //addColor({ .4f, .4f, .1f }); // yellow

        ////addColor({ .4f, .4f, .3f }); // yellowish
        ////addColor({ .2f, .2f, .15f }); // strip yellow

        //addColor({ .2f, .4f, .3f }); // Greenish
        //addColor({ .02f, .3f, .29f }); // teal (custom)
        //addColor({ .25f, .5f, .15f }); // bright green
        //addColor({ .05f, .4f, .2f }); // green
        //addColor({ .16f, .48f, .32f }); // bright teal
        //addColor({ .16f, .32f, .16f }); // strip green

        //addColor({ .1f, .24f, .55f }); // bright blue
        //addColor({ .05f, .15f, .40f }); // blue
        //addColor({ .07f, .14f, .28f }); // strip blue
        //addColor({ .12f, .12f, .43f }); // deep blue
        //addColor({});
        //addColor({});

        //addColor({ 2.0f, .6f, 1.2f }); // super purple
        //addColor({ .3f, .05f, .4f }); // purple
        //addColor({ .4f, .35f, .45f }); // bright purple
        //addColor({ .24f, .24f, .48f }); // purple
        //addColor({ .14f, .12f, .20f }); // purple
        //addColor({ .3f, .25f, .40f }); // purplish

        // Boost brightness
        //for (auto& c : palette) {
        //    c *= 2.5f;
        //    c.w = 1;
        //}

        return palette;
    }

    Array<Color, PALETTE_SIZE> LoadPalette(ryml::NodeRef node) {
        Array<Color, PALETTE_SIZE> palette{};

        int i = 0;
        auto addColor = [&i, &palette](const Color& color) {
            if (i >= palette.size()) return;
            palette[i++] = color;
        };

        if (!node.valid() || node.is_seed()) {
            return GetDefaultPalette();
        }

        for (const auto& child : node.children()) {
            Color color;
            ReadValue(child, color);
            addColor(color);
        }

        return palette;
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
        ReadValue(node["Multithread"], settings.Multithread);
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
            if (auto key = magic_enum::enum_cast<Input::Keys>(tokens.back()))
                binding.Key = *key;

            // Note that it is valid for Key to equal None to indicate that the user unbound it on purpose
            bindings.Add(binding);
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

    void SaveGameBindings(ryml::NodeRef node) {
        node |= ryml::SEQ;

        for (auto& binding : Game::Bindings.GetBindings()) {
            auto child = node.append_child();
            child |= ryml::MAP;
            auto action = magic_enum::enum_name(binding.Action);
            if (binding.Key != Input::Keys::None) {
                auto key = string(magic_enum::enum_name(binding.Key));
                child[ryml::to_csubstr(action.data())] << key;
            }
            else if (binding.Mouse != Input::MouseButtons::None) {
                auto btn = string(magic_enum::enum_name(binding.Mouse));
                child[ryml::to_csubstr(action.data())] << btn;
            }
        }
    }

    void LoadGameBindings(ryml::NodeRef node) {
        if (node.is_seed()) return;

        Game::Bindings.Clear(); // we have some bindings to replace defaults!

        for (const auto& c : node.children()) {
            if (c.is_seed() || !c.is_map()) continue;

            auto kvp = c.child(0);
            string value, command;
            if (kvp.has_key()) command = string(kvp.key().data(), kvp.key().len);
            if (kvp.has_val()) value = string(kvp.val().data(), kvp.val().len);
            if (value.empty() || command.empty()) continue;

            GameBinding binding;
            if (auto commandName = magic_enum::enum_cast<GameAction>(command))
                binding.Action = *commandName;

            // The binding could either be a key or a mouse button
            if (auto key = magic_enum::enum_cast<Input::Keys>(value))
                binding.Key = *key;

            if (auto btn = magic_enum::enum_cast<Input::MouseButtons>(value))
                binding.Mouse = *btn;

            Game::Bindings.Add(binding);
        }
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
        node["ShowPortals"] << s.ShowPortals;
        node["WireframeOpacity"] << s.WireframeOpacity;

        node["ShowWireframe"] << s.ShowWireframe;
        node["RenderMode"] << (int)s.RenderMode;
        node["GizmoSize"] << s.GizmoSize;
        node["CrosshairSize"] << s.CrosshairSize;
        node["InvertY"] << s.InvertY;
        node["InvertOrbitY"] << s.InvertOrbitY;
        node["MiddleMouseMode"] << (int)s.MiddleMouseMode;
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
        node["PasteSegmentObjects"] << s.PasteSegmentObjects;
        node["PasteSegmentWalls"] << s.PasteSegmentWalls;
        node["PasteSegmentSpecial"] << s.PasteSegmentSpecial;
        node["TexturePreviewSize"] << (int)s.TexturePreviewSize;
        node["ShowLevelTitle"] << s.ShowLevelTitle;
        node["ShowTerrain"] << s.ShowTerrain;

        SaveSelectionSettings(node["Selection"], s.Selection);
        SaveOpenWindows(node["Windows"], s.Windows);
        SaveLightSettings(node["Lighting"], s.Lighting);
        SavePalette(node["Palette"], s.Palette);
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
        ReadValue(node["ShowPortals"], s.ShowPortals);
        ReadValue(node["WireframeOpacity"], s.WireframeOpacity);

        ReadValue(node["ShowWireframe"], s.ShowWireframe);
        ReadValue(node["RenderMode"], (int&)s.RenderMode);
        ReadValue(node["GizmoSize"], s.GizmoSize);
        ReadValue(node["CrosshairSize"], s.CrosshairSize);
        ReadValue(node["InvertY"], s.InvertY);
        ReadValue(node["InvertOrbitY"], s.InvertOrbitY);
        ReadValue(node["MiddleMouseMode"], (int&)s.MiddleMouseMode);
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
        ReadValue(node["EnablePhysics"], s.EnablePhysics);
        ReadValue(node["PasteSegmentObjects"], s.PasteSegmentObjects);
        ReadValue(node["PasteSegmentWalls"], s.PasteSegmentWalls);
        ReadValue(node["PasteSegmentSpecial"], s.PasteSegmentSpecial);
        ReadValue(node["TexturePreviewSize"], (int&)s.TexturePreviewSize);
        ReadValue(node["ShowLevelTitle"], s.ShowLevelTitle);
        ReadValue(node["ShowTerrain"], s.ShowTerrain);

        s.Palette = LoadPalette(node["Palette"]);
        s.Selection = LoadSelectionSettings(node["Selection"]);
        s.Windows = LoadOpenWindows(node["Windows"]);
        s.Lighting = LoadLightSettings(node["Lighting"]);
        return s;
    }

    void SaveCheatSettings(ryml::NodeRef node, const CheatSettings& s) {
        node |= ryml::MAP;

        node["DisableAI"] << s.DisableAI;
        node["DisableWeaponDamage"] << s.DisableWeaponDamage;
    }

    CheatSettings LoadCheatSettings(ryml::NodeRef node) {
        CheatSettings s;
        if (node.is_seed()) return s;

        ReadValue(node["DisableAI"], s.DisableAI);
        ReadValue(node["DisableWeaponDamage"], s.DisableWeaponDamage);
        return s;
    }

    void SaveGameSettings(ryml::NodeRef node, const InfernoSettings& settings) {
        node |= ryml::MAP;

        node["ShipWiggle"] << (int)settings.ShipWiggle;
        node["InvertY"] << settings.InvertY;
        node["MouseSensitivity"] << settings.MouseSensitivity;
        node["Difficulty"] << (int)Game::Difficulty;
        node["HalvePitchSpeed"] << Settings::Inferno.HalvePitchSpeed;
        node["ShipAutolevel"] << Settings::Inferno.ShipAutolevel;
        node["NoAutoselectWhileFiring"] << Settings::Inferno.NoAutoselectWhileFiring;
        node["AutoselectAfterFiring"] << Settings::Inferno.AutoselectAfterFiring;
        node["StickyRearview"] << Settings::Inferno.StickyRearview;
        node["SlowmoFusion"] << Settings::Inferno.SlowmoFusion;
    }

    void LoadGameSettings(ryml::NodeRef node, InfernoSettings& settings) {
        if (node.is_seed()) return;

        ReadValue(node["ShipWiggle"], settings.ShipWiggle);
        ReadValue(node["InvertY"], settings.InvertY);
        ReadValue(node["MouseSensitivity"], settings.MouseSensitivity);
        ReadValue(node["Difficulty"], Game::Difficulty);
        ReadValue(node["HalvePitchSpeed"], settings.HalvePitchSpeed);
        ReadValue(node["ShipAutolevel"], settings.ShipAutolevel);
        ReadValue(node["NoAutoselectWhileFiring"], settings.NoAutoselectWhileFiring);
        ReadValue(node["AutoselectAfterFiring"], settings.AutoselectAfterFiring);
        ReadValue(node["StickyRearview"], settings.StickyRearview);
        ReadValue(node["SlowmoFusion"], settings.SlowmoFusion);

        Game::Difficulty = (DifficultyLevel)std::clamp((int)Game::Difficulty, 0, 4);
    }

    void Settings::Save(const filesystem::path& path) {
        try {
            ryml::Tree doc(128, 128);
            doc.rootref() |= ryml::MAP;

            doc["Descent1Path"] << Settings::Inferno.Descent1Path.string();
            doc["Descent2Path"] << Settings::Inferno.Descent2Path.string();
            doc["MasterVolume"] << Settings::Inferno.MasterVolume;
            doc["MusicVolume"] << Settings::Inferno.MusicVolume;
            doc["EffectVolume"] << Settings::Inferno.EffectVolume;
            doc["GenerateMaps"] << Settings::Inferno.GenerateMaps;
            doc["Descent3Enhanced"] << Settings::Inferno.Descent3Enhanced;

            doc["Fullscreen"] << Settings::Inferno.Fullscreen;
            doc["Maximized"] << Settings::Inferno.Maximized;
            doc["WindowSize"] << EncodeVector(Settings::Inferno.WindowSize);
            doc["WindowPosition"] << EncodeVector(Settings::Inferno.WindowPosition);

            SaveGameSettings(doc["Game"], Settings::Inferno);
            WriteSequence(doc["DataPaths"], Settings::Inferno.DataPaths);
            SaveEditorSettings(doc["Editor"], Settings::Editor);
            SaveGraphicsSettings(doc["Render"], Settings::Graphics);
            SaveCheatSettings(doc["Cheats"], Settings::Cheats);

            {
                auto bindings = doc["Bindings"];
                bindings |= ryml::MAP;
                SaveEditorBindings(bindings["Editor"]);
                SaveGameBindings(bindings["Game"]);
            }

            std::ofstream file(path);
            file << doc;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving config file:\n{}", e.what());
        }
    }

    void Settings::Load(const filesystem::path& path) {
        try {
            std::ifstream file(path);
            if (!file) return;

            std::stringstream buffer;
            buffer << file.rdbuf();
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(buffer.str()));
            ryml::NodeRef root = doc.rootref();

            if (root.is_map()) {
                ReadValue(root["Descent1Path"], Settings::Inferno.Descent1Path);
                ReadValue(root["Descent2Path"], Settings::Inferno.Descent2Path);
                ReadValue(root["MasterVolume"], Settings::Inferno.MasterVolume);
                ReadValue(root["MusicVolume"], Settings::Inferno.MusicVolume);
                ReadValue(root["EffectVolume"], Settings::Inferno.EffectVolume);
                ReadValue(root["GenerateMaps"], Settings::Inferno.GenerateMaps);
                ReadValue(root["Descent3Enhanced"], Settings::Inferno.Descent3Enhanced);

                ReadValue(root["Fullscreen"], Settings::Inferno.Fullscreen);
                ReadValue(root["Maximized"], Settings::Inferno.Maximized);
                ReadValue(root["WindowSize"], Settings::Inferno.WindowSize);
                ReadValue(root["WindowPosition"], Settings::Inferno.WindowPosition);

                auto dataPaths = root["DataPaths"];
                if (!dataPaths.is_seed()) {
                    for (const auto& c : dataPaths.children()) {
                        filesystem::path dataPath;
                        ReadValue(c, dataPath);
                        if (!dataPath.empty()) Settings::Inferno.DataPaths.push_back(dataPath);
                    }
                }

                LoadGameSettings(root["Game"], Settings::Inferno);
                Settings::Editor = LoadEditorSettings(root["Editor"], Settings::Inferno);
                Settings::Graphics = LoadGraphicsSettings(root["Render"]);
                Settings::Cheats = LoadCheatSettings(root["Cheats"]);

                auto bindings = root["Bindings"];
                if (!bindings.is_seed()) {
                    LoadEditorBindings(bindings["Editor"]);
                    LoadGameBindings(bindings["Game"]);
                }

                Settings::Editor.Windows.Debug = true; // Always show debug window for alpha
            }
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading config file:\n{}", e.what());
        }
    }
}

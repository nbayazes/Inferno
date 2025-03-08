#include "pch.h"

#define MAGIC_ENUM_RANGE_MIN 0
#define MAGIC_ENUM_RANGE_MAX 256

#include <Settings.h>
#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include <fstream>
#include <magic_enum/magic_enum.hpp>
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
        node["FieldOfView"] << s.FieldOfView;
        node["Brightness"] << s.Brightness;
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

        ReadValue(node["FieldOfView"], s.FieldOfView);
        s.FieldOfView = std::clamp(s.FieldOfView, 60.0f, 100.0f);

        ReadValue(node["Brightness"], s.Brightness);

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

        if (!node.readable()) {
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

    void WriteSensitivity(ryml::NodeRef parent, const InputDeviceBinding::Sensitivity& sensitivity) {
        auto node = parent["sensitivity"];
        node |= ryml::MAP;

        node["thrust"] << EncodeVector(sensitivity.thrust);
        node["rotation"] << EncodeVector(sensitivity.rotation);

        node["thrustDeadzone"] << EncodeVector(sensitivity.thrustDeadzone);
        node["rotationDeadzone"] << EncodeVector(sensitivity.rotationDeadzone);
    }

    void ReadSensitivity(ryml::ConstNodeRef parent, InputDeviceBinding::Sensitivity& sensitivity) {
        if (!parent.has_child("sensitivity")) return;

        auto node = parent["sensitivity"];

        ReadValue2(node, "thrust", sensitivity.thrust);
        ReadValue2(node, "rotation", sensitivity.rotation);
        ReadValue2(node, "thrustDeadzone", sensitivity.thrustDeadzone);
        ReadValue2(node, "rotationDeadzone", sensitivity.rotationDeadzone);
    }

    void SaveGameBindings(ryml::NodeRef root) {
        root |= ryml::MAP;

        {
            auto devicesNode = root["InputDevices"];
            devicesNode |= ryml::SEQ;

            for (auto& device : Game::Bindings.GetDevices()) {
                auto deviceNode = devicesNode.append_child();
                deviceNode |= ryml::MAP;
                deviceNode["guid"] << device.guid;
                deviceNode["type"] << string(magic_enum::enum_name(device.type));

                WriteSensitivity(deviceNode, device.sensitivity);

                auto actionList = deviceNode["actions"];
                actionList |= ryml::SEQ;

                for (size_t i = 0; i < (int)GameAction::Count; i++) {
                    if (device.IsUnset((GameAction)i))
                        continue;

                    auto actionNode = actionList.append_child();
                    actionNode |= ryml::MAP;
                    //actionNode["action"] << i;
                    actionNode["action"] << string(magic_enum::enum_name((GameAction)i));

                    for (size_t slot = 0; slot < BIND_SLOTS; slot++) {
                        auto& binding = device.bindings[i][slot];
                        if (binding.type == BindType::None) continue;

                        auto bindingNode = actionNode[slot == 0 ? "bind" : "bind2"];
                        bindingNode |= ryml::MAP;

                        bindingNode["id"] << binding.id;

                        if (binding.type != BindType::Button)
                            bindingNode["type"] << ToUnderlying(binding.type);

                        switch (binding.type) {
                            case BindType::Axis:
                            case BindType::AxisPlus:
                            case BindType::AxisMinus:
                                if (binding.invert)
                                    bindingNode["invert"] << binding.invert;

                                break;
                            case BindType::AxisButtonPlus:
                                break;
                            case BindType::AxisButtonMinus:
                                break;
                            case BindType::Hat:
                                break;
                        }
                    }
                }
            }
        }

        {
            auto& keyboard = Game::Bindings.GetKeyboard();
            auto keyboardNode = root["Keyboard"];
            keyboardNode |= ryml::MAP;

            WriteSensitivity(keyboardNode, keyboard.sensitivity);

            auto actionList = keyboardNode["actions"];
            actionList |= ryml::SEQ;

            for (size_t i = 0; i < (int)GameAction::Count; i++) {
                if (keyboard.IsUnset((GameAction)i)) continue;
                auto actionNode = actionList.append_child();
                actionNode |= ryml::MAP;
                actionNode["action"] << string(magic_enum::enum_name((GameAction)i));

                for (size_t j = 0; j < BIND_SLOTS; j++) {
                    auto& binding = keyboard.bindings[i][j];
                    if (binding.type == BindType::None) continue;

                    auto bindingNode = actionNode[j == 0 ? "bind" : "bind2"];
                    bindingNode |= ryml::MAP;
                    bindingNode["id"] << binding.id;
                }
            }
        }

        {
            auto& mouse = Game::Bindings.GetMouse();
            auto mouseNode = root["Mouse"];
            mouseNode |= ryml::MAP;

            WriteSensitivity(mouseNode, mouse.sensitivity);

            auto actionList = mouseNode["actions"];
            actionList |= ryml::SEQ;

            for (size_t i = 0; i < (int)GameAction::Count; i++) {
                if (mouse.IsUnset((GameAction)i)) continue;
                auto actionNode = actionList.append_child();
                actionNode |= ryml::MAP;
                actionNode["action"] << string(magic_enum::enum_name((GameAction)i));

                for (size_t j = 0; j < BIND_SLOTS; j++) {
                    auto& binding = mouse.bindings[i][j];
                    if (binding.type == BindType::None) continue;

                    auto bindingNode = actionNode[j == 0 ? "bind" : "bind2"];
                    bindingNode |= ryml::MAP;
                    bindingNode["id"] << binding.id;

                    if (binding.type != BindType::Button)
                        bindingNode["type"] << ToUnderlying(binding.type);

                    if (binding.type == BindType::Axis && binding.invert)
                        bindingNode["invert"] << binding.invert;
                }
            }
        }
    }

    void ReadBinding(ryml::ConstNodeRef node, InputDeviceBinding& device) {
        GameBinding binding;

        string action;
        if (ReadValue2(node, "action", action)) {
            if (auto key = magic_enum::enum_cast<GameAction>(action))
                binding.action = *key;
        }

        auto readBindGroup = [&](ryml::ConstNodeRef root, int slot) {
            ReadValue2(root, "id", binding.id);
            ReadValue2(root, "type", (std::underlying_type_t<BindType>&)binding.type);
            ReadValue2(root, "invert", binding.invert);
            device.Bind(binding, slot);
        };

        if (auto bindNode = GetNode(node, "bind"))
            readBindGroup(*bindNode, 0);

        if (auto bindNode = GetNode(node, "bind2"))
            readBindGroup(*bindNode, 1);
    }

    void LoadGameBindings(ryml::ConstNodeRef node) {
        if (auto devices = GetSequenceNode(node, "InputDevices")) {
            for (const auto& deviceNode : *devices) {
                string guid;
                if (!ReadValue2(deviceNode, "guid", guid))
                    continue; // Missing guid!

                string type;
                if (!ReadValue2(deviceNode, "type", type))
                    continue; // Missing type!

                auto inputType = Input::InputType::Unknown;
                if (auto key = magic_enum::enum_cast<Input::InputType>(type))
                    inputType = *key;

                if (inputType == Input::InputType::Unknown)
                    continue; // Missing type!

                auto& device = Game::Bindings.AddDevice(guid, inputType);
                ReadSensitivity(deviceNode, device.sensitivity);

                auto actions = GetSequenceNode(deviceNode, "actions");
                if (!actions) continue;

                for (const auto& actionNode : *actions) {
                    ReadBinding(actionNode, device);
                }
            }
        }

        if (auto keyboardNode = GetNode(node, "Keyboard")) {
            auto& keyboard = Game::Bindings.GetKeyboard();

            ReadSensitivity(*keyboardNode, keyboard.sensitivity);

            if (auto actions = GetSequenceNode(*keyboardNode, "actions")) {
                for (const auto& action : *actions) {
                    ReadBinding(action, keyboard);
                }
            }
        }
        else {
            ResetKeyboardBindings(Game::Bindings.GetKeyboard());
        }

        if (auto mouseNode = GetNode(node, "Mouse")) {
            auto& mouse = Game::Bindings.GetMouse();
            ReadSensitivity(*mouseNode, mouse.sensitivity);

            if (auto actions = GetSequenceNode(*mouseNode, "actions")) {
                for (const auto& action : *actions) {
                    ReadBinding(action, mouse);
                }
            }
        }
        else {
            ResetMouseBindings(Game::Bindings.GetMouse());
        }
    }

    void SaveEditorSettings(ryml::NodeRef node, const EditorSettings& s) {
        node |= ryml::MAP;
        WritePaths(node["RecentFiles"], s.RecentFiles);

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

        ReadValue2(node, "EnableWallMode", s.EnableWallMode);
        ReadValue2(node, "EnableTextureMode", s.EnableTextureMode);
        ReadValue2(node, "ObjectRenderDistance", s.ObjectRenderDistance);

        ReadValue2(node, "TranslationSnap", s.TranslationSnap);
        ReadValue2(node, "RotationSnap", s.RotationSnap);

        ReadValue2(node, "MouselookSensitivity", s.MouselookSensitivity);
        ReadValue2(node, "MoveSpeed", s.MoveSpeed);

        ReadValue2(node, "SelectionMode", (int&)s.SelectionMode);
        ReadValue2(node, "InsertMode", (int&)s.InsertMode);

        ReadValue2(node, "ShowObjects", s.ShowObjects);
        ReadValue2(node, "ShowWalls", s.ShowWalls);
        ReadValue2(node, "ShowTriggers", s.ShowTriggers);
        ReadValue2(node, "ShowFlickeringLights", s.ShowFlickeringLights);
        ReadValue2(node, "ShowAnimation", s.ShowAnimation);
        ReadValue2(node, "ShowMatcenEffects", s.ShowMatcenEffects);
        ReadValue2(node, "ShowPortals", s.ShowPortals);
        ReadValue2(node, "WireframeOpacity", s.WireframeOpacity);

        ReadValue2(node, "ShowWireframe", s.ShowWireframe);
        ReadValue2(node, "RenderMode", (int&)s.RenderMode);
        ReadValue2(node, "GizmoSize", s.GizmoSize);
        ReadValue2(node, "CrosshairSize", s.CrosshairSize);
        ReadValue2(node, "InvertY", s.InvertY);
        ReadValue2(node, "InvertOrbitY", s.InvertOrbitY);
        ReadValue2(node, "MiddleMouseMode", (int&)s.MiddleMouseMode);
        ReadValue2(node, "FieldOfView", s.FieldOfView);
        s.FieldOfView = std::clamp(s.FieldOfView, 45.0f, 130.0f);
        ReadValue2(node, "FontSize", s.FontSize);
        s.FontSize = std::clamp(s.FontSize, 8, 48);

        ReadValue2(node, "EditBothWallSides", s.EditBothWallSides);
        ReadValue2(node, "ReopenLastLevel", s.ReopenLastLevel);
        ReadValue2(node, "SelectMarkedSegment", s.SelectMarkedSegment);
        ReadValue2(node, "ResetUVsOnAlign", s.ResetUVsOnAlign);
        ReadValue2(node, "WeldTolerance", s.WeldTolerance);

        ReadValue2(node, "Undos", s.UndoLevels);
        ReadValue2(node, "AutosaveMinutes", s.AutosaveMinutes);
        ReadValue2(node, "CoordinateSystem", (int&)s.CoordinateSystem);
        ReadValue2(node, "EnablePhysics", s.EnablePhysics);
        ReadValue2(node, "PasteSegmentObjects", s.PasteSegmentObjects);
        ReadValue2(node, "PasteSegmentWalls", s.PasteSegmentWalls);
        ReadValue2(node, "PasteSegmentSpecial", s.PasteSegmentSpecial);
        ReadValue2(node, "TexturePreviewSize", (int&)s.TexturePreviewSize);
        ReadValue2(node, "ShowLevelTitle", s.ShowLevelTitle);
        ReadValue2(node, "ShowTerrain", s.ShowTerrain);

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

        ReadValue2(node, "DisableAI", s.DisableAI);
        ReadValue2(node, "DisableWeaponDamage", s.DisableWeaponDamage);
        return s;
    }

    void SavePriorities(ryml::NodeRef node, span<const uint8> priorities) {
        node |= ryml::SEQ;
        for (auto& i : priorities)
            node.append_child() << i;
    }

    void SaveGameSettings(ryml::NodeRef node, const InfernoSettings& settings) {
        node |= ryml::MAP;

        node["ShipWiggle"] << (int)settings.ShipWiggle;
        node["InvertY"] << settings.InvertY;
        node["Difficulty"] << (int)Game::Difficulty;
        node["HalvePitchSpeed"] << settings.HalvePitchSpeed;
        node["ShipAutolevel"] << settings.ShipAutolevel;
        node["NoAutoselectWhileFiring"] << settings.NoAutoselectWhileFiring;
        node["AutoselectAfterFiring"] << settings.AutoselectAfterFiring;
        node["StickyRearview"] << settings.StickyRearview;
        node["SlowmoFusion"] << settings.SlowmoFusion;
        node["PreferHighResFonts"] << settings.PreferHighResFonts;
        node["UseSoundOcclusion"] << settings.UseSoundOcclusion;
        node["UseTextureCaching"] << settings.UseTextureCaching;
        node["RecentMission"] << settings.RecentMission;

        SavePriorities(node["PrimaryPriority"], settings.PrimaryPriority);
        SavePriorities(node["SecondaryPriority"], settings.SecondaryPriority);
    }

    void ReadPriorities(ryml::NodeRef node, span<uint8> priorities) {
        if (!node.is_seed() && node.readable() && node.is_seq()) {
            uint8 i = 0;
            for (const auto& child : node.children()) {
                int value;
                if (ReadValue(child, value))
                    priorities[i++] = (uint8)value;

                if (i >= priorities.size()) break;
            }
        }
    }

    void LoadGameSettings(ryml::NodeRef node, InfernoSettings& settings) {
        if (node.is_seed()) return;

        ReadValue2(node, "ShipWiggle", settings.ShipWiggle);
        ReadValue2(node, "InvertY", settings.InvertY);
        ReadValue2(node, "Difficulty", Game::Difficulty);
        ReadValue2(node, "HalvePitchSpeed", settings.HalvePitchSpeed);
        ReadValue2(node, "ShipAutolevel", settings.ShipAutolevel);
        ReadValue2(node, "NoAutoselectWhileFiring", settings.NoAutoselectWhileFiring);
        ReadValue2(node, "AutoselectAfterFiring", settings.AutoselectAfterFiring);
        ReadValue2(node, "StickyRearview", settings.StickyRearview);
        ReadValue2(node, "SlowmoFusion", settings.SlowmoFusion);
        ReadValue2(node, "PreferHighResFonts", settings.PreferHighResFonts);
        ReadValue2(node, "UseSoundOcclusion", settings.UseSoundOcclusion);
        ReadValue2(node, "UseTextureCaching", settings.UseTextureCaching);
        ReadValue2(node, "RecentMission", settings.RecentMission);

        ReadPriorities(node["PrimaryPriority"], settings.PrimaryPriority);
        ReadPriorities(node["SecondaryPriority"], settings.SecondaryPriority);

        Game::Difficulty = (DifficultyLevel)std::clamp((int)Game::Difficulty, 0, 4);
    }

    void Settings::Save(const filesystem::path& path) {
        try {
            SPDLOG_INFO("Saving settings to {}", path.string());

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
            WritePaths(doc["DataPaths"], Settings::Inferno.DataPaths);
            SaveEditorSettings(doc["Editor"], Settings::Editor);
            SaveGraphicsSettings(doc["Render"], Settings::Graphics);
            SaveCheatSettings(doc["Cheats"], Settings::Cheats);

            {
                auto bindings = doc["Bindings"];
                bindings |= ryml::MAP;
                SaveEditorBindings(bindings["Editor"]);
                SaveGameBindings(bindings["Game"]);
            }

            filesystem::path temp = path;
            temp.replace_filename("temp.cfg");

            {
                std::ofstream file(temp);
                file << doc;
            }

            // Write went okay, ovewrite the old file and remove temp
            filesystem::copy(temp, path, filesystem::copy_options::overwrite_existing);
            filesystem::remove(temp);
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error saving config file:\n{}", e.what());
        }
    }

    void Settings::Load(const filesystem::path& path) {
        try {
            std::ifstream file(path);
            if (!file) return;

            SPDLOG_INFO("Loading settings from {}", path.string());

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

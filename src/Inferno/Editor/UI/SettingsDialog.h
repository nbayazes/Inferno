#pragma once

#include "WindowBase.h"
#include "WindowsDialogs.h"

namespace Inferno::Editor {
    class SettingsDialog : public ModalWindowBase {
        Array<char, MAX_PATH> _d1PathBuffer{}, _d2PathBuffer{};
        bool _enableForegroundFpsLimit = false;
        const std::array<int, 4> _msaaSamples = { 1, 2, 4, 8 };

        int _selectedPath{};
        TexturePreviewSize _texturePreviewSize = TexturePreviewSize::Medium;

        EditorSettings _editor;
        InfernoSettings _inferno;
        GraphicsSettings _graphics;

        EditorBindings _bindings;

        struct BindingEntry {
            EditorAction Action{};
            string Label;
            EditorBinding Primary, Secondary;
        };

        List<BindingEntry> _bindingEntries;

    public:
        SettingsDialog() : ModalWindowBase("Settings") {
            Width = 800 * Shell::DpiScale;
            EnableCloseHotkeys = false;
        }

    protected:
        void MainOptionsTab() {
            if (!ImGui::BeginTabItem("Options")) return;

            static constexpr COMDLG_FILTERSPEC filter[] = { { L"Executable", L"*.exe" } };

            ImGui::Text("Descent 1 executable");

            strcpy_s(_d1PathBuffer.data(), MAX_PATH, _inferno.Descent1Path.string().c_str());
            if (ImGui::InputTextEx("##d1exe", nullptr, _d1PathBuffer.data(), MAX_PATH, { -100 * Shell::DpiScale, 0 }, 0)) {
                _inferno.Descent1Path = string(_d1PathBuffer.data());
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse...##d1", { 90 * Shell::DpiScale, 0 }))
                if (auto folder = OpenFileDialog(filter, L"Pick game executable"))
                    _inferno.Descent1Path = *folder;

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Descent 2 executable");

            strcpy_s(_d2PathBuffer.data(), MAX_PATH, _inferno.Descent2Path.string().c_str());
            if (ImGui::InputTextEx("##d2exe", nullptr, _d2PathBuffer.data(), MAX_PATH, { -100 * Shell::DpiScale, 0 }, 0)) {
                _inferno.Descent2Path = string(_d2PathBuffer.data());
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse...##d2", { 90 * Shell::DpiScale, 0 }))
                if (auto folder = OpenFileDialog(filter, L"Pick game executable"))
                    _inferno.Descent2Path = *folder;

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Separator();

            const auto labelWidth = 165 * Shell::DpiScale;
            const auto columnHeight = 425 * Shell::DpiScale;
            ImGui::BeginChild("left", { Width / 2 - 25 * Shell::DpiScale, columnHeight });


            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, labelWidth);

            {
                ImGui::TextDisabled("Camera");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabel("Invert mouselook Y");
                ImGui::Checkbox("##invert", &_editor.InvertY);
                ImGui::NextColumn();

                ImGui::ColumnLabel("Middle click orbits");
                bool middleOrbit = _editor.MiddleMouseMode == MiddleMouseMode::Orbit;
                if (ImGui::Checkbox("##use-orbit", &middleOrbit))
                    _editor.MiddleMouseMode = middleOrbit ? MiddleMouseMode::Orbit : MiddleMouseMode::Mouselook;

                ImGui::NextColumn();

                {
                    ImGui::ColumnLabel("Invert orbit");

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("X");
                    ImGui::SameLine();
                    ImGui::Checkbox("##invert-orbit-x", &_editor.InvertOrbitX);

                    ImGui::SameLine(0, 20);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Y");
                    ImGui::SameLine();
                    ImGui::Checkbox("##invert-orbit-y", &_editor.InvertOrbitY);

                    ImGui::NextColumn();
                }

                ImGui::ColumnLabelEx("Sensitivity", "How sensitive the camera is in mouselook mode");
                ImGui::SetNextItemWidth(-1);

                auto sensitivity = _editor.MouselookSensitivity * 1000;
                if (ImGui::SliderFloat("##mlook", &sensitivity, 1, 10, "%.2f"))
                    _editor.MouselookSensitivity = sensitivity / 1000;
                ImGui::NextColumn();

                ImGui::ColumnLabel("Speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##Speed", &_editor.MoveSpeed, 40, 300, "%.0f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Field of view");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##FOV", &_editor.FieldOfView, 55, 120, "%.0f");
                ImGui::NextColumn();
            }

            {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::TextDisabled("Graphics");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("MSAA", "Multisample antialiasinging\n\nReduces jagged edges of polygons.\nHas a potentially high performance impact.");
                ImGui::SetNextItemWidth(-1);

                auto msaa = [](int samples) {
                    switch (samples) {
                        default:
                        case 1: return 0;
                        case 2: return 1;
                        case 4: return 2;
                        case 8: return 3;
                    }
                }(_graphics.MsaaSamples);

                if (ImGui::SliderInt("##MSAA", &msaa, 0, (int)std::size(_msaaSamples) - 1, std::to_string(_msaaSamples[msaa]).c_str())) {
                    _graphics.MsaaSamples = [&msaa]() {
                        switch (msaa) {
                            default:
                            case 0: return 1;
                            case 1: return 2;
                            case 2: return 4;
                            case 3: return 8;
                        }
                    }();
                }
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Texture filtering", "Also enables high-res replacement textures");
                ImGui::Checkbox("##filtering", &_graphics.HighRes);
                ImGui::NextColumn();

                {
                    DisableControls disable(!Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT());
                    ImGui::ColumnLabelEx("Bloom", "Bloom is an effect that has no impact on the level.\nCustom emissive textures are suggested to appear correctly.\n\nRequires a GPU that supports typed UAV loads");
                    ImGui::Checkbox("##Bloom", &_graphics.EnableBloom);
                    ImGui::NextColumn();
                }

                ImGui::ColumnLabel("Wireframe opacity");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##wfopacity", &_editor.WireframeOpacity, 0, 1, "%.2f");
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
            ImGui::EndChild();

            ImGui::SameLine(0, 10 * Shell::DpiScale);

            ImGui::BeginChild("right", { Width / 2 - 25, columnHeight });
            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, labelWidth);
            {
                ImGui::TextDisabled("Editor");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Undos", "Must reload the level to take effect");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##Undos", &_editor.UndoLevels, 1, 5);
                ImGui::NextColumn();

                ImGui::ColumnLabel("Gizmo size");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##gizmo", &_editor.GizmoSize, 0.1f, 2.5, 10, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Crosshair size");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##Crosshair", &_editor.CrosshairSize, 0.1f, 0.1f, 2, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Weld Tolerance");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##Weld", &_editor.WeldTolerance, 0.1f, 0.1f, 5, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Object distance", "Max distance to draw sprites and models for objects");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##drawdist", &_editor.ObjectRenderDistance, 0, 1500, "%.0f");
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Text size", "Must restart the editor to take effect");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##font", &_editor.FontSize, 18, 32);
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Autosave", "Zero is off");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##autosave", &_editor.AutosaveMinutes, 0, 60, "%d min");
                ImGui::NextColumn();
            }

            {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::TextDisabled("FPS limits");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Foreground", "Limit the foreground FPS to prevent high power usage\nor heat on some systems.");
                ImGui::SetNextItemWidth(-40);
                {
                    DisableControls disable(!_enableForegroundFpsLimit);
                    ImGui::SliderInt("##Foreground", &_graphics.ForegroundFpsLimit, 30, 120);
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("##enablelimit", &_enableForegroundFpsLimit)) {
                    if (!_enableForegroundFpsLimit) _graphics.ForegroundFpsLimit = -1;
                }

                ImGui::NextColumn();

                ImGui::ColumnLabel("Background");
                ImGui::SetNextItemWidth(-40);
                ImGui::SliderInt("##Background", &_graphics.BackgroundFpsLimit, 1, 30);
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::EndChild();

            ImGui::Checkbox("Reset UVs on alignment", &_editor.ResetUVsOnAlign);
            ImGui::HelpMarker("Resets the UVs of marked faces when\nusing the align marked command");

            ImGui::Checkbox("Select segment when marking", &_editor.SelectMarkedSegment);
            ImGui::HelpMarker("Enable to select the clicked segment when\nmarking connected faces (Ctrl+Shift+Click)");

            ImGui::Checkbox("Reopen last level on start", &_editor.ReopenLastLevel);

            ImGui::Checkbox("Show level title", &_editor.ShowLevelTitle);

            ImGui::Text("Texture preview size");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150 * Shell::DpiScale);
            ImGui::Combo("##texpreview", (int*)&_texturePreviewSize, "Small\0Medium\0Large");
            ImGui::EndTabItem();
        }

        void KeyBindingsTab() {
            if (!ImGui::BeginTabItem("Shortcuts")) return;

            if (ImGui::Button("Reset to defaults")) {
                _bindingEntries = BuildBindingEntries(Bindings::Default);
            }

            ImGui::BeginChild("container");

            constexpr auto flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("binds", 4, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed/*, ImGuiTableColumnFlags_DefaultSort, 0.0f*/);
                ImGui::TableSetupColumn("Shortcut");
                ImGui::TableSetupColumn("Alt Shortcut");
                ImGui::TableHeadersRow();

                using Keys = DirectX::Keyboard::Keys;
                static int selectedBinding = -1;
                static bool editAlt = false;

                for (int i = 0; i < _bindingEntries.size(); i++) {
                    ImGui::PushID(i);
                    auto& binding = _bindingEntries[i];

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text(binding.Label.c_str());
                    ImGui::TableNextColumn();
                    ImVec2 bindBtnSize = { 150 * Shell::DpiScale, 0 };

                    if (i == selectedBinding && !editAlt) {
                        if (ImGui::Button("Press a key...", bindBtnSize))
                            selectedBinding = -1;
                    }
                    else {
                        auto label = binding.Primary.Key == Keys::None ? "None" : binding.Primary.GetShortcutLabel();
                        if (ImGui::Button(label.c_str(), bindBtnSize)) {
                            selectedBinding = i;
                            editAlt = false;
                        }
                    }

                    const ImVec2 clearBtnSize = { 40 * Shell::DpiScale, 0 };
                    ImGui::SameLine(0, 1);
                    if (binding.Primary.Key == Keys::None) {
                        ImGui::Dummy(clearBtnSize);
                    }
                    else {
                        if (ImGui::ButtonEx("X", clearBtnSize))
                            binding.Primary.ClearShortcut();
                    }

                    ImGui::TableNextColumn();
                    if (binding.Action != EditorAction::HoldMouselook) {
                        ImGui::PushID(10);
                        if (i == selectedBinding && editAlt) {
                            if (ImGui::Button("Press a key...", bindBtnSize))
                                selectedBinding = -1;
                        }
                        else {
                            auto label = binding.Secondary.Key == Keys::None ? "None" : binding.Secondary.GetShortcutLabel();
                            if (ImGui::Button(label.c_str(), bindBtnSize)) {
                                selectedBinding = i;
                                editAlt = true;
                            }
                        }

                        ImGui::SameLine(0, 1);
                        if (binding.Secondary.Key == Keys::None) {
                            ImGui::Dummy(clearBtnSize);
                        }
                        else {
                            if (ImGui::Button("X", clearBtnSize))
                                binding.Secondary.ClearShortcut();
                        }

                        ImGui::PopID();
                    }

                    ImGui::TableNextColumn();
                    ImGui::SameLine();

                    ImGui::PopID();
                }

                // In bind mode - capture the next pressed key
                if (selectedBinding != -1) {
                    for (Keys key = Keys::Back; key <= Keys::OemClear; key = Keys((unsigned char)key + 1)) {
                        if (Bindings::IsReservedKey(key)) continue;

                        if (Input::IsKeyDown(key)) {
                            // assign the new binding
                            auto& entry = _bindingEntries[selectedBinding];
                            EditorBinding binding = editAlt ? entry.Secondary : entry.Primary; // copy
                            binding.Key = key;
                            binding.Alt = Input::AltDown;
                            binding.Shift = Input::ShiftDown;
                            binding.Control = Input::ControlDown;
                            if (binding.Action == EditorAction::HoldMouselook)
                                binding.Alt = binding.Shift = binding.Control = false;

                            UnbindExisting(_bindingEntries, binding);

                            if (editAlt)
                                _bindingEntries[selectedBinding].Secondary = binding;
                            else
                                _bindingEntries[selectedBinding].Primary = binding;

                            selectedBinding = -1;
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        void DataPathsTab() {
            if (!ImGui::BeginTabItem("Data Paths")) return;

            ImGui::Text("Extra paths to search for game data. Paths that appear LAST have higher priority.\nDrag to reorder.");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::BeginChild("container");

            const auto buttonWidth = 130 * Shell::DpiScale;
            auto& dataPaths = _inferno.DataPaths;

            {
                ImGui::BeginChild("data paths list", { Width - buttonWidth - 25 * Shell::DpiScale, 400 * Shell::DpiScale }, true);

                for (int i = 0; i < dataPaths.size(); i++) {
                    if (ImGui::Selectable(dataPaths[i].string().c_str(), _selectedPath == i))
                        _selectedPath = i;

                    if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                        int n_next = i + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                        if (n_next >= 0 && n_next < dataPaths.size()) {
                            std::swap(dataPaths[i], dataPaths[n_next]);
                            _selectedPath = n_next;
                            ImGui::ResetMouseDragDelta();
                        }
                    }
                }

                ImGui::EndChild();
            }

            ImGui::SameLine();

            {
                ImGui::BeginChild("list btns", { buttonWidth, -1 });

                if (ImGui::Button("Add...", { -1, 0 })) {
                    if (auto path = BrowseFolderDialog()) {
                        dataPaths.push_back(*path);
                    }
                }

                if (ImGui::Button("Remove", { -1, 0 })) {
                    Seq::removeAt(dataPaths, _selectedPath);
                    if (_selectedPath > 0) _selectedPath--;
                }

                ImGui::EndChild();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        void OnUpdate() override {
            ImGui::BeginChild("prop_panel", { -1, 800 * Shell::DpiScale });

            if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
                MainOptionsTab();
                KeyBindingsTab();
                DataPathsTab();
                ImGui::EndTabBar();
            }
            ImGui::EndChild();

            AcceptButtons();
        }

        bool OnOpen() override {
            _bindingEntries = BuildBindingEntries(Bindings::Active);
            _inferno = Settings::Inferno;
            _editor = Settings::Editor;
            _graphics = Settings::Graphics;
            _enableForegroundFpsLimit = Settings::Graphics.ForegroundFpsLimit != -1;

            if (!Resources::HasGameData()) {
                ShowOkMessage(L"Game data was not found, please configure the executable paths.\n\n"
                              L"If game data is not in the same folder as the executable, use the Data Paths tab to add the folders containing descent.hog and descent2.hog",
                              L"Missing game data");
            }
            return true;
        }

        void OnAccept() override {
            CopyBindingEntries(_bindingEntries);

            bool resourcesChanged = false;
            auto dataPathsChanged = _inferno.DataPaths != Settings::Inferno.DataPaths;
            if (dataPathsChanged || _inferno.Descent1Path != Settings::Inferno.Descent1Path || _inferno.Descent2Path != Settings::Inferno.Descent2Path) {
                resourcesChanged = true;
            }

            if (_graphics.HighRes != Settings::Graphics.HighRes) {
                resourcesChanged = true;
            }

            if (_graphics.MsaaSamples != Settings::Graphics.MsaaSamples) {
                resourcesChanged = true;
            }

            Settings::Inferno = _inferno;
            Settings::Editor = _editor;
            Settings::Graphics = _graphics;
            Settings::Save();
            Events::SettingsChanged();

            if (resourcesChanged) {
                FileSystem::Init();
                Resources::LoadLevel(Game::Level);
                Render::LoadLevel(Game::Level);
                Render::Materials->LoadLevelTextures(Game::Level, true);
                Render::Adapter->ReloadResources();
            }
        }

        static void UnbindExisting(span<BindingEntry> entries, const EditorBinding& binding) {
            for (auto& entry : entries) {
                if (entry.Primary == binding) entry.Primary.ClearShortcut();
                if (entry.Secondary == binding) entry.Secondary.ClearShortcut();
            }
        }

        static List<BindingEntry> BuildBindingEntries(EditorBindings bindings /*copy*/) {
            bindings.Sort();
            List<BindingEntry> entries;

            for (auto& binding : bindings.GetBindings()) {
                if (auto existing = Seq::find(entries, [&binding](const BindingEntry& e) { return e.Action == binding.Action; })) {
                    existing->Secondary = binding;
                }
                else {
                    BindingEntry entry{};
                    auto& cmd = GetCommandForAction(binding.Action);
                    entry.Label = cmd.Name;
                    entry.Action = binding.Action;
                    entry.Primary = binding;
                    entries.push_back(entry);
                }
            }

            return entries;
        }

        static void CopyBindingEntries(span<BindingEntry> entries) {
            using Keys = DirectX::Keyboard::Keys;
            Bindings::Active.Clear();

            for (auto& entry : entries) {
                if (entry.Primary.Key != Keys::None)
                    Bindings::Active.Add(entry.Primary);

                if (entry.Secondary.Key != Keys::None)
                    Bindings::Active.Add(entry.Secondary);

                // Save bindings set to 'none' in case the user unbinds them
                if (entry.Primary.Key == Keys::None && entry.Secondary.Key == Keys::None)
                    Bindings::Active.Add(entry.Primary);
            }
        }
    };
}

#pragma once

#include "WindowBase.h"
#include "WindowsDialogs.h"

namespace Inferno::Editor {
    class SettingsDialog : public ModalWindowBase {
        Array<char, MAX_PATH> _d1PathBuffer, _d2PathBuffer;
        filesystem::path _descent1Path, _descent2Path;
        string _d1exe, _d2exe;
        float _mouselookSensitivity;
        float _objectDrawDistance;
        float _weldTolerance;
        int _fieldOfView, _cameraSpeed;
        int _foregroundFpsLimit, _backgroundFpsLimit;
        bool _enableForegroundFpsLimit;
        float _gizmoSize, _crosshairSize;
        int _undos;
        bool _invertY, _textureFiltering, _bloom;
        bool _resetUvsOnAlign, _selectMarkedSegment;
        bool _reopenLastLevel;
        int _msaa;
        int _fontSize;
        int _autosaveMinutes;
        float _wireframeOpacity;
        const std::array<int, 4> _msaaSamples = { 1, 2, 4, 8 };

        int _selectedPath;
        bool _dataPathsChanged = false;
        TexturePreviewSize _texturePreviewSize;
    public:
        SettingsDialog() : ModalWindowBase("Settings") {
            Width = 800 * Shell::DpiScale;
        }

    protected:
        void MainOptionsTab() {
            if (!ImGui::BeginTabItem("Options")) return;

            static const COMDLG_FILTERSPEC filter[] = { { L"Executable", L"*.exe" } };

            ImGui::Text("Descent 1 executable");

            strcpy_s(_d1PathBuffer.data(), MAX_PATH, _descent1Path.string().c_str());
            if (ImGui::InputTextEx("##d1exe", nullptr, _d1PathBuffer.data(), MAX_PATH, { -100 * Shell::DpiScale, 0 }, 0)) {
                _descent1Path = string(_d1PathBuffer.data());
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse...##d1", { 90 * Shell::DpiScale, 0 }))
                if (auto folder = OpenFileDialog(filter, L"Pick game executable"))
                    _descent1Path = *folder;

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Descent 2 executable");

            strcpy_s(_d2PathBuffer.data(), MAX_PATH, _descent2Path.string().c_str());
            if (ImGui::InputTextEx("##d2exe", nullptr, _d2PathBuffer.data(), MAX_PATH, { -100 * Shell::DpiScale, 0 }, 0)) {
                _descent2Path = string(_d2PathBuffer.data());
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse...##d2", { 90 * Shell::DpiScale, 0 }))
                if (auto folder = OpenFileDialog(filter, L"Pick game executable"))
                    _descent2Path = *folder;

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Separator();

            const auto labelWidth = 165 * Shell::DpiScale;
            const auto columnHeight = 375 * Shell::DpiScale;
            ImGui::BeginChild("left", { Width / 2 - 25 * Shell::DpiScale, columnHeight });


            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, labelWidth);

            {
                ImGui::TextDisabled("Camera");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabel("Invert Y");
                ImGui::Checkbox("##invert", &_invertY);
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Sensitivity", "How sensitive the camera is in mouselook mode");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##mlook", &_mouselookSensitivity, 1, 10, "%.2f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##Speed", &_cameraSpeed, 40, 300);
                ImGui::NextColumn();

                ImGui::ColumnLabel("Field of view");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##FOV", &_fieldOfView, 55, 120);
                ImGui::NextColumn();
            }

            {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::TextDisabled("Graphics");
                ImGui::NextColumn();
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("MSAA", "Multisample antialiasinging\n\nReduces jagged edges of polygons.\nHas a potentially high performance impact.");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##MSAA", &_msaa, 0, (int)std::size(_msaaSamples) - 1, std::to_string(_msaaSamples[_msaa]).c_str()))
                    _msaa = std::clamp(_msaa, 0, 3);
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Texture filtering", "Also enables high-res replacement textures");
                ImGui::Checkbox("##filtering", &_textureFiltering);
                ImGui::NextColumn();

                {
                    DisableControls disable(!Render::Adapter->TypedUAVLoadSupport_R11G11B10_FLOAT());
                    ImGui::ColumnLabelEx("Bloom", "Bloom is an effect that has no impact on the level.\nCustom emissive textures are suggested to appear correctly.\n\nRequires a GPU that supports typed UAV loads");
                    ImGui::Checkbox("##Bloom", &_bloom);
                    ImGui::NextColumn();
                }

                ImGui::ColumnLabel("Wireframe opacity");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##wfopacity", &_wireframeOpacity, 0, 1, "%.2f");
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
                ImGui::InputInt("##Undos", &_undos, 1, 5);
                ImGui::NextColumn();

                ImGui::ColumnLabel("Gizmo size");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##gizmo", &_gizmoSize, 0.1f, 2.5, 10, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Crosshair size");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##Crosshair", &_crosshairSize, 0.1f, 0.1f, 2, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabel("Weld Tolerance");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##Weld", &_weldTolerance, 0.1f, 0.1f, 5, "%.1f");
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Object distance", "Max distance to draw sprites and models for objects");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##drawdist", &_objectDrawDistance, 0, 1500, "%.0f");
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Text size", "Must restart the editor to take effect");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##font", &_fontSize, 18, 32);
                ImGui::NextColumn();

                ImGui::ColumnLabelEx("Autosave", "Zero is off");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderInt("##autosave", &_autosaveMinutes, 0, 60, "%d min");
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
                    ImGui::SliderInt("##Foreground", &_foregroundFpsLimit, 30, 120);
                }
                ImGui::SameLine();
                ImGui::Checkbox("##enablelimit", &_enableForegroundFpsLimit);
                ImGui::NextColumn();

                ImGui::ColumnLabel("Background");
                ImGui::SetNextItemWidth(-40);
                ImGui::SliderInt("##Background", &_backgroundFpsLimit, 1, 30);
                ImGui::NextColumn();
            }

            ImGui::Columns(1);
            ImGui::EndChild();

            ImGui::Checkbox("Reset UVs on alignment", &_resetUvsOnAlign);
            ImGui::HelpMarker("Resets the UVs of marked faces when\nusing the align marked command");

            ImGui::Checkbox("Select segment when marking", &_selectMarkedSegment);
            ImGui::HelpMarker("Enable to select the clicked segment when\nmarking connected faces (Ctrl+Shift+Click)");

            ImGui::Checkbox("Reopen last level on start", &_reopenLastLevel);

            ImGui::Text("Texture preview size");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150 * Shell::DpiScale);
            ImGui::Combo("##texpreview", (int*)&_texturePreviewSize, "Small\0Medium\0Large");
            ImGui::EndTabItem();
        }

        void DataPathsTab() {
            if (!ImGui::BeginTabItem("Data Paths")) return;

            ImGui::Text("Extra paths to search for game data. Paths that appear LAST have higher priority.\nDrag to reorder.");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::BeginChild("container");

            const auto buttonWidth = 130 * Shell::DpiScale;

            {
                ImGui::BeginChild("data paths list", { Width - buttonWidth - 25 * Shell::DpiScale, 400 * Shell::DpiScale }, true);

                for (int i = 0; i < Settings::DataPaths.size(); i++) {
                    if (ImGui::Selectable(Settings::DataPaths[i].string().c_str(), _selectedPath == i))
                        _selectedPath = i;

                    if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                        int n_next = i + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                        if (n_next >= 0 && n_next < Settings::DataPaths.size()) {
                            std::swap(Settings::DataPaths[i], Settings::DataPaths[n_next]);
                            _selectedPath = n_next;
                            _dataPathsChanged = true;
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
                        Settings::DataPaths.push_back(*path);
                        _dataPathsChanged = true;
                    }
                }

                if (ImGui::Button("Remove", { -1, 0 })) {
                    Seq::removeAt(Settings::DataPaths, _selectedPath);
                    _dataPathsChanged = true;
                    if (_selectedPath > 0) _selectedPath--;
                }

                ImGui::EndChild();
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        void OnUpdate() override {

            ImGui::BeginChild("prop_panel", { -1, 750 * Shell::DpiScale });

            if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
                MainOptionsTab();
                DataPathsTab();
                ImGui::EndTabBar();
            }
            ImGui::EndChild();

            AcceptButtons();
        }

        bool OnOpen() override {
            _descent1Path = Settings::Descent1Path;
            _descent2Path = Settings::Descent2Path;
            _mouselookSensitivity = Settings::MouselookSensitivity * 1000;
            _objectDrawDistance = Settings::ObjectRenderDistance;
            _invertY = Settings::InvertY;
            _cameraSpeed = (int)Settings::MoveSpeed;
            _fieldOfView = (int)Settings::FieldOfView;
            _textureFiltering = Settings::HighRes;
            _bloom = Settings::EnableBloom;
            _undos = Settings::UndoLevels;
            _gizmoSize = Settings::GizmoSize;
            _crosshairSize = Settings::CrosshairSize;
            _weldTolerance = Settings::WeldTolerance;
            _fontSize = Settings::FontSize;
            _enableForegroundFpsLimit = Settings::ForegroundFpsLimit != -1;
            _foregroundFpsLimit = _enableForegroundFpsLimit ? Settings::ForegroundFpsLimit : 60;
            _backgroundFpsLimit = Settings::BackgroundFpsLimit;
            _resetUvsOnAlign = Settings::ResetUVsOnAlign;
            _selectMarkedSegment = Settings::SelectMarkedSegment;
            _reopenLastLevel = Settings::ReopenLastLevel;
            _autosaveMinutes = Settings::AutosaveMinutes;
            _texturePreviewSize = Settings::TexturePreviewSize;
            _wireframeOpacity = Settings::WireframeOpacity;

            switch (Settings::MsaaSamples) {
                case 1: _msaa = 0; break;
                case 2: _msaa = 1; break;
                case 4: _msaa = 2; break;
                case 8: _msaa = 3; break;
            }
            return true;
        }

        void OnAccept() override {
            Settings::MouselookSensitivity = _mouselookSensitivity / 1000;
            Settings::ObjectRenderDistance = _objectDrawDistance;
            Settings::InvertY = _invertY;
            Settings::MoveSpeed = (float)_cameraSpeed;
            Settings::FieldOfView = (float)_fieldOfView;
            Settings::EnableBloom = _bloom;
            Settings::UndoLevels = _undos;
            Settings::GizmoSize = _gizmoSize;
            Settings::CrosshairSize = _crosshairSize;
            Settings::WeldTolerance = _weldTolerance;
            Settings::FontSize = _fontSize;
            Settings::ForegroundFpsLimit = _enableForegroundFpsLimit ? _foregroundFpsLimit : -1;
            Settings::BackgroundFpsLimit = _backgroundFpsLimit;
            Settings::ResetUVsOnAlign = _resetUvsOnAlign;
            Settings::SelectMarkedSegment = _selectMarkedSegment;
            Settings::ReopenLastLevel = _reopenLastLevel;
            Settings::AutosaveMinutes = _autosaveMinutes;
            Settings::TexturePreviewSize = _texturePreviewSize;
            Settings::WireframeOpacity = _wireframeOpacity;

            bool resourcesChanged = false;
            if (_dataPathsChanged || _descent1Path != Settings::Descent1Path || _descent2Path != Settings::Descent2Path) {
                Settings::Descent1Path = _descent1Path;
                Settings::Descent2Path = _descent2Path;
                FileSystem::Init();
                resourcesChanged = true;
            }

            if (_textureFiltering != Settings::HighRes) {
                Settings::HighRes = _textureFiltering;
                resourcesChanged = true;
            }

            int msaa = _msaa;
            switch (_msaa) {
                case 0: msaa = 1; break;
                case 1: msaa = 2; break;
                case 2: msaa = 4; break;
                case 3: msaa = 8; break;
            }

            if (msaa != Settings::MsaaSamples) {
                Settings::MsaaSamples = msaa;
                resourcesChanged = true;
            }

            Settings::Save();
            Events::SettingsChanged();

            if (resourcesChanged) {
                Resources::LoadLevel(Game::Level);
                Render::LoadLevel(Game::Level);
                Render::Materials->LoadLevelTextures(Game::Level, true);
                Render::Adapter->ReloadResources();
            }
        }
    };
}
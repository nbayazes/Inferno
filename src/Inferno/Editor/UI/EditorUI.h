#pragma once
#include "DebugWindow.h"
#include "MissionEditor.h"
#include "BloomWindow.h"
#include "LightingWindow.h"
#include "StatusBar.h"
#include "NoiseWindow.h"
#include "TextureBrowserUI.h"
#include "HogEditor.h"
#include "PropertyEditor.h"
#include "SettingsDialog.h"
#include "NewLevelDialog.h"
#include "ReactorEditor.h"
#include "HelpDialog.h"
#include "TunnelBuilderWindow.h"
#include "AboutDialog.h"
#include "SoundBrowser.h"
#include "DiagnosticWindow.h"
#include "BriefingEditor.h"
#include "ScaleWindow.h"
#include "TextureEditor.h"
#include "MaterialEditor.h"
#include "TerrainEditor.h"

namespace Inferno::Editor {
    class GotoSegmentDialog : public ModalWindowBase {
        int _value = 0;
        int _maxValue = 0;

    public:
        GotoSegmentDialog() : ModalWindowBase("Go To Segment") {
            Width = 350;
        }

    protected:
        bool OnOpen() override {
            _value = (int)Editor::Selection.Segment;
            _maxValue = (int)Game::Level.Segments.size() - 1;
            return true;
        }

        void OnUpdate() override {
            ImGui::Text("Segment Number 0 - %i", _maxValue);

            SetInitialFocus();
            if (ImGui::InputInt("##input", &_value, 0))
                _value = std::clamp(_value, 0, _maxValue);
            EndInitialFocus();

            AcceptButtons("OK", "Cancel");
        }

        void OnAccept() override {
            Editor::Selection.SetSelection({ (SegID)_value });
        }
    };

    class GotoObjectDialog : public ModalWindowBase {
        int _value = 0;
        int _maxValue = 0;

    public:
        GotoObjectDialog() : ModalWindowBase("Go To Object") {
            Width = 350;
        }

    protected:
        bool OnOpen() override {
            _value = (int)Editor::Selection.Object;
            _maxValue = (int)Game::Level.Objects.size() - 1;
            return true;
        }

        void OnUpdate() override {
            ImGui::Text("Object Number 0 - %i", _maxValue);

            SetInitialFocus();
            if (ImGui::InputInt("##input", &_value, 0))
                _value = std::clamp(_value, 0, _maxValue);
            EndInitialFocus();

            AcceptButtons("OK", "Cancel");
        }

        void OnAccept() override {
            Editor::Selection.SetSelection((ObjID)_value);
        }
    };

    class GotoWallDialog : public ModalWindowBase {
        int _value = 0;
        int _maxValue = 0;

    public:
        GotoWallDialog() : ModalWindowBase("Go To Wall") {
            Width = 350;
        }

    protected:
        bool OnOpen() override {
            _value = (int)Editor::Selection.Object;
            _maxValue = (int)Game::Level.Walls.size() - 1;
            return true;
        }

        void OnUpdate() override {
            ImGui::Text("Wall Number 0 - %i", _maxValue);

            SetInitialFocus();
            if (ImGui::InputInt("##input", &_value, 0))
                _value = std::clamp(_value, 0, _maxValue);
            EndInitialFocus();

            AcceptButtons("OK", "Cancel");
        }

        void OnAccept() override {
            if (auto wall = Game::Level.TryGetWall((WallID)_value))
                Editor::Selection.SetSelection(wall->Tag);
        }
    };

    class GotoRoomDialog : public ModalWindowBase {
        int _value = 0;
        int _maxValue = 0;

    public:
        GotoRoomDialog() : ModalWindowBase("Go To Room") {
            Width = 350;
        }

    protected:
        bool OnOpen() override {
            _maxValue = (int)Game::Level.Rooms.size() - 1;
            return true;
        }

        void OnUpdate() override {
            ImGui::Text("Room Number 0 - %i", _maxValue);

            SetInitialFocus();
            if (ImGui::InputInt("##input", &_value, 0))
                _value = std::clamp(_value, 0, _maxValue);
            EndInitialFocus();

            AcceptButtons("OK", "Cancel");
        }

        void OnAccept() override {
            for (size_t i = 0; i < Game::Level.Segments.size(); i++) {
                if (Game::Level.Segments[i].Room == (RoomID)_value)
                    Editor::Selection.SetSelection({ (SegID)i });
            }
        }
    };

    class EditorUI {
        TextureBrowserUI _textureBrowser;
        TextureEditor _textureEditor;
        PropertyEditor _propertyEditor;
        DebugWindow _debugWindow;
        BloomWindow _bloomWindow;
        LightingWindow _lightingWindow;
        StatusBar _statusBar;
        NoiseWindow _noise;
        ReactorEditor _reactorEditor;
        TunnelBuilderWindow _tunnelBuilder;
        SoundBrowser _sounds;
        DiagnosticWindow _diagnosticWindow;
        BriefingEditor _briefingEditor;
        ScaleWindow _scaleWindow;
        MaterialEditor _materialEditor;
        TerrainEditor _terrainEditor;
        bool _showImguiDemo = false;

        Dictionary<DialogType, Ptr<ModalWindowBase>> _dialogs;
        List<Ptr<WindowBase>> _windows;
        float _mainMenuHeight = 30;

        template <class TModal>
        void RegisterDialog(DialogType type) {
            _dialogs[type] = MakePtr<TModal>();
        }

        template <class TWindow>
        TWindow* RegisterWindow() {
            auto window = make_unique<TWindow>();
            auto ptr = window.get();
            _windows.push_back(std::move(window));
            return ptr;
        }

    public:
        EditorUI() {
            RegisterDialog<GotoSegmentDialog>(DialogType::GotoSegment);
            RegisterDialog<GotoObjectDialog>(DialogType::GotoObject);
            RegisterDialog<GotoWallDialog>(DialogType::GotoWall);
            RegisterDialog<GotoRoomDialog>(DialogType::GotoRoom);
            RegisterDialog<RenameLevelDialog>(DialogType::RenameLevel);
            RegisterDialog<MissionEditor>(DialogType::MissionEditor);
            RegisterDialog<NewLevelDialog>(DialogType::NewLevel);
            RegisterDialog<HogEditor>(DialogType::HogEditor);
            RegisterDialog<SettingsDialog>(DialogType::Settings);
            RegisterDialog<HelpDialog>(DialogType::Help);
            RegisterDialog<AboutDialog>(DialogType::About);

            RegisterWindow<ReactorEditor>();
            RegisterWindow<NoiseWindow>();
            RegisterWindow<TunnelBuilderWindow>();
            RegisterWindow<SoundBrowser>();
            RegisterWindow<BloomWindow>();
            RegisterWindow<DebugWindow>();
            RegisterWindow<LightingWindow>();
            RegisterWindow<PropertyEditor>();
            RegisterWindow<TextureBrowserUI>();
            RegisterWindow<TextureEditor>();
            RegisterWindow<DiagnosticWindow>();
            RegisterWindow<BriefingEditor>();
            RegisterWindow<ScaleWindow>();
            //RegisterWindow<ProjectToPlaneWindow>();
            //RegisterWindow<InsetFacesWindow>();
            RegisterWindow<TerrainEditor>();
            RegisterWindow<MaterialEditor>();

            Events::ShowDialog += [this](DialogType type) {
                // Don't show another dialog if one is already open as it will confuse imgui state
                if (ImGui::GetTopMostPopupModal()) return;
                if (auto& dialog = _dialogs[type]) dialog->Show();
            };

            _bloomWindow.IsOpen(false);
            _statusBar.IsOpen(true);
        }

        void OnRender();

    protected:
        void DrawMenu();
        void DrawDockspace(const ImGuiViewport* viewport) const;
        ImGuiDockNode* CreateDockLayout(ImGuiID dockspaceId, const ImGuiViewport* viewport) const;
    };

    inline float TopToolbarOffset = 0; // Used for offsetting the level title text
    inline float MainViewportXOffset = 0, MainViewportWidth = 0;
}

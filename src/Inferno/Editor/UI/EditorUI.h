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

namespace Inferno::Editor {

    class GotoSegmentDialog : public ModalWindowBase {
        int _value = 0;
        int _maxValue = 0;

    public:
        GotoSegmentDialog() : ModalWindowBase("Go To Segment") {
            Width = 350;
        };

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
        bool _showImguiDemo = false;

        Dictionary<DialogType, Ptr<ModalWindowBase>> _dialogs;
        float _mainMenuHeight = 30;

        template<class TModal>
        void RegisterDialog(DialogType type) {
            _dialogs[type] = MakePtr<TModal>();
        }
    public:
        EditorUI() {
            RegisterDialog<GotoSegmentDialog>(DialogType::GotoSegment);
            RegisterDialog<RenameLevelDialog>(DialogType::RenameLevel);
            RegisterDialog<MissionEditor>(DialogType::MissionEditor);
            RegisterDialog<NewLevelDialog>(DialogType::NewLevel);
            RegisterDialog<HogEditor>(DialogType::HogEditor);
            RegisterDialog<SettingsDialog>(DialogType::Settings);
            RegisterDialog<HelpDialog>(DialogType::Help);
            RegisterDialog<AboutDialog>(DialogType::About);

            Events::ShowDialog += [this](DialogType type) {
                if (auto& dialog = _dialogs[type]) dialog->Show();
            };

            _bloomWindow.IsOpen(false);
            _statusBar.IsOpen(true);
        }

        void OnRender();

    protected:
        void DrawMenu();
        void DrawDockspace(const ImGuiViewport* viewport);
        ImGuiDockNode* CreateDockLayout(ImGuiID dockspaceId, const ImGuiViewport* viewport);

    };

    inline float TopToolbarOffset = 0; // Used for offsetting the level title text
    inline float MainViewportXOffset = 0, MainViewportWidth = 0;
}

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
#include "InsetFacesWindow.h"
#include "ScaleWindow.h"
#include "TextureEditor.h"
#include "ProjectToPlaneWindow.h"
#include "InsetFacesWindow.h"

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
        StatusBar _statusBar;
        bool _showImguiDemo = false;

        Dictionary<DialogType, Ptr<ModalWindowBase>> _dialogs;
        List<Ptr<WindowBase>> _windows;
        float _mainMenuHeight = 30;

        template<class TModal>
        void RegisterDialog(DialogType type) {
            _dialogs[type] = MakePtr<TModal>();
        }

        template<class TWindow>
        TWindow* RegisterWindow() {
            auto window = make_unique<TWindow>();
            auto ptr = window.get();
            _windows.push_back(std::move(window));
            return ptr;
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
            //RegisterWindow<BriefingEditor>();
            RegisterWindow<ScaleWindow>();
            RegisterWindow<ProjectToPlaneWindow>();
            RegisterWindow<InsetFacesWindow>();

            Events::ShowDialog += [this](DialogType type) {
                // Don't show another dialog if one is already open as it will confuse imgui state
                if (ImGui::GetTopMostPopupModal()) return;
                if (auto& dialog = _dialogs[type]) dialog->Show();
            };

            _statusBar.IsOpen(true);
        }

        void OnRender();

    protected:
        void DrawMenu();
        void DrawDockspace(const ImGuiViewport* viewport) const;
        ImGuiDockNode* CreateDockLayout(ImGuiID dockspaceId, const ImGuiViewport* viewport) const;

    };

    inline bool ShowDebugOverlay = false;
    inline float TopToolbarOffset = 0; // Used for offsetting the level title text
    inline float MainViewportXOffset = 0, MainViewportWidth = 0;
}

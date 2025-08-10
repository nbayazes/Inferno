#pragma once

#include "Editor/Bindings.h"
#include "Settings.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    class SettingsDialog final : public ModalWindowBase {
        Array<char, MAX_PATH> _d1PathBuffer{}, _d2PathBuffer{};
        bool _enableForegroundFpsLimit = false;
        static constexpr std::array<int, 4> _msaaSamples = { 1, 2, 4, 8 };

        int _selectedPath = 0;

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
        SettingsDialog() : ModalWindowBase("Settings", ImGuiWindowFlags_NoCollapse) {
            Width = 800 * Shell::DpiScale;
            Height = 800 * Shell::DpiScale;
            EnableCloseHotkeys = false;
        }

    protected:
        void MainOptionsTab();

        void KeyBindingsTab();

        void DataPathsTab();

        void OnUpdate() override;

        bool OnOpen() override;

        bool OnAccept() override;

        static void UnbindExisting(span<BindingEntry> entries, const EditorBinding& binding);

        static List<BindingEntry> BuildBindingEntries(EditorBindings bindings /*copy*/);

        static void CopyBindingEntries(span<BindingEntry> entries);
    };

}

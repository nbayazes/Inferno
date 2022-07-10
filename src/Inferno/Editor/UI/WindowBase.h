#pragma once

#include "imgui_local.h"

namespace ImGui {
    inline void HelpMarker(const char* desc) {
        SameLine();
        TextDisabled("(?)");
        if (IsItemHovered()) {
            BeginTooltip();
            PushTextWrapPos(GetFontSize() * 35.0f);
            TextUnformatted(desc);
            PopTextWrapPos();
            EndTooltip();
        }
    }

    // Identical to TextInput but fills horizontal space
    inline bool TextInputWide(std::string_view label, std::string& str, int maxSize) {
        if (str.size() < maxSize)
            str.resize(maxSize);

        ImGui::Text(label.data());
        std::string id("##");
        id.append(label);
        return ImGui::InputTextEx(id.data(), nullptr, str.data(), maxSize, { -1, 0 }, 0);
    }

    template<class...TArgs>
    void ColumnLabel(const char* label, TArgs&& ...args) {
        ImGui::AlignTextToFramePadding();   // Text and Tree nodes are less high than framed widgets, here we add vertical spacing to make the tree lines equal high.
        ImGui::Text(label, std::forward<TArgs>(args)...);
        ImGui::NextColumn();
    }

    template<class...TArgs>
    void ColumnLabelEx(const char* label, const char* desc, TArgs&& ...args) {
        ImGui::AlignTextToFramePadding();   // Text and Tree nodes are less high than framed widgets, here we add vertical spacing to make the tree lines equal high.
        ImGui::Text(label, std::forward<TArgs>(args)...);
        ImGui::HelpMarker(desc);
        ImGui::NextColumn();
    }

    template<class...TArgs>
    void TableRowLabel(const char* label, TArgs&& ...args) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();   // Text and Tree nodes are less high than framed widgets, here we add vertical spacing to make the tree lines equal high.
        ImGui::Text(label, std::forward<TArgs>(args)...);
        ImGui::TableNextColumn();
    }

    template<class...TArgs>
    void TableRowLabelEx(const char* label, const char* desc, TArgs&& ...args) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();   // Text and Tree nodes are less high than framed widgets, here we add vertical spacing to make the tree lines equal high.
        ImGui::Text(label, std::forward<TArgs>(args)...);
        ImGui::HelpMarker(desc);
        ImGui::TableNextColumn();
    }
}

namespace Inferno::Editor {
    constexpr ImGuiWindowFlags ToolbarFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    constexpr ImGuiWindowFlags MainWindowFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;


    template<class TFlag>
    bool FlagCheckbox(const char* label, TFlag flagToCheck, TFlag& value) {
        bool isChecked = (int)value & (int)flagToCheck;
        bool changed = false;
        if (ImGui::Checkbox(label, &isChecked)) {
            changed = true;
            value = isChecked ?
                TFlag((int)value | (int)flagToCheck) :
                TFlag((int)value ^ (int)flagToCheck);
        }

        return changed;
    }

    inline void DrawHeader(const char* text, const ImColor& color = { 70, 70, 70 }) {
        auto window = ImGui::GetCurrentWindow();
        auto width = window->Size.x - window->ScrollbarSizes.x;
        auto y = window->DC.CursorPos.y;
        auto y1 = y + ImGui::GetFontSize() + 4;

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(window->Pos, { window->Pos.x + width, window->Pos.y + window->Size.y });
        drawList->AddRectFilled({ window->Pos.x, y }, { window->Pos.x + width, y1 }, color);
        drawList->PopClipRect();

        ImGui::Text(text);
        ImGui::Spacing();
    }

    // Disables all controls in the current scope
    class DisableControls {
        bool _condition;
    public:
        DisableControls(bool condition) : _condition(condition) {
            if (!condition) return;
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        ~DisableControls() {
            if (!_condition) return;
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }

        DisableControls(const DisableControls&) = delete;
        DisableControls(DisableControls&&) = delete;
        DisableControls& operator=(const DisableControls&) = delete;
        DisableControls& operator=(DisableControls&&) = delete;
    };

    class WindowBase {
        string _name;
        ImGuiWindowFlags _flags;
        bool _isOpen = false;
        bool* _pIsOpen = nullptr;
    public:
        WindowBase(string name, bool* open = nullptr, ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse)
            : _name(name), _flags(flags), _pIsOpen(open) {
            if (!open) _pIsOpen = &_isOpen;
        }

        virtual ~WindowBase() = default;

        float DefaultWidth = 450, DefaultHeight = 450;

        void Update() {
            if (!IsOpen()) return;

            ImGui::SetNextWindowSize({ DefaultWidth, DefaultHeight }, ImGuiCond_FirstUseEver);
            BeforeUpdate();

            if (ImGui::Begin(Name(), _pIsOpen, _flags))
                OnUpdate();

            ImGui::End();

            AfterUpdate();
        }

        bool IsOpen() { return *_pIsOpen; }
        void IsOpen(bool open) {
            *_pIsOpen = open;
            OnOpen(open);
        }

        void ToggleIsOpen() { IsOpen(!IsOpen()); }
        const char* Name() { return _name.c_str(); }

        ImGuiWindow* GetHandle() {
            return ImGui::FindWindowByName(Name());
        }

    protected:
        virtual void BeforeUpdate() {};
        virtual void OnUpdate() = 0;
        virtual void AfterUpdate() {};
        virtual void OnOpen(bool) {}
    };

    class ModalWindowBase {
        ImGuiWindowFlags _flags;
        bool _focused = false;
        bool IsOpen = false;
    public:
        ModalWindowBase(string name, ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)
            : Name(name), _flags(flags) {}
        virtual ~ModalWindowBase() = default;

        void Update() {
            if (IsOpen)
                ImGui::OpenPopup(Name.c_str());

            ImGui::SetNextWindowSize({ Width, Height });
            if (ImGui::BeginPopupModal(Name.c_str(), &IsOpen, _flags)) {
                OnUpdate();

                if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape)))
                    Close();

                if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && !ImGui::GetIO().WantTextInput)
                    Close(true);

                ImGui::EndPopup();
            }
        }

        string Name;
        float Width = 500;
        float Height = -1;

        void Show() {
            IsOpen = OnOpen();
            _focused = false;
        }

        void Close(bool accepted = false) {
            IsOpen = false;
            ImGui::CloseCurrentPopup();
            if (accepted) OnAccept();
            else OnCancel();

            if (Callback) Callback(accepted);
        }

        // Passes true if the user accepted the dialog
        std::function<void(bool)> Callback;


    protected:
        virtual void OnUpdate() = 0;
        virtual void OnAccept() {}
        virtual void OnCancel() {}
        virtual bool OnOpen() { return true; }

        void AcceptButtons(const char* acceptLabel = "OK", const char* cancelLabel = "Cancel", bool canAccept = true) {
            ImGui::Dummy({ 0, 10 });

            ImGui::BeginChild("closebtns", { 0, 32 });
            ImGui::SameLine(ImGui::GetWindowWidth() - 205);

            {
                DisableControls disable(!canAccept);
                if (ImGui::Button(acceptLabel, { 100, 0 }))
                    Close(true);
            }

            ImGui::SameLine(ImGui::GetWindowWidth() - 100);
            if (ImGui::Button(cancelLabel, { 100, 0 }))
                Close();
            ImGui::EndChild();
        }

        void CloseButton(const char* acceptLabel = "OK", bool canAccept = true) {
            ImGui::Dummy({ 0, 10 });

            ImGui::BeginChild("closebtns", { 0, 32 });
            ImGui::SameLine(ImGui::GetWindowWidth() - 100);

            {
                DisableControls disable(!canAccept);
                if (ImGui::Button(acceptLabel, { 100, 0 }))
                    Close(true);
            }

            ImGui::EndChild();
        }

        // Sets the next element to get focus when the modal opens
        void SetInitialFocus() {
            if (!_focused) ImGui::SetKeyboardFocusHere();
        }

        // Must follow the item marked by SetInitialFocus();
        void EndInitialFocus() {
            if (ImGui::IsItemActive()) _focused = true;
        }

    };

    /*class ImGuiWindow {
        bool _isVisible;
    public:
        ImGuiWindow(const char* name, bool* isOpen = nullptr, ImGuiWindowFlags flags = 0) {
            _isVisible = ImGui::Begin(name, isOpen, flags);
        }

        bool IsVisible() { return _isVisible; }

        ~ImGuiWindow() {
            ImGui::End();
        }
    };*/

}
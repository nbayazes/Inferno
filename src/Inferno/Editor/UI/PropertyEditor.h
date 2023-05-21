#pragma once

#include "WindowBase.h"
#include "Level.h"
#include "Graphics/MaterialLibrary.h"
#include "../Editor.h"
#include "MatcenEditor.h"

namespace Inferno::Editor {
    inline bool SegmentDropdown(SegID& id) {
        bool changed = false;
        auto label = std::to_string((int)id);

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##segs", label.c_str())) {
            for (int i = 0; i < Game::Level.Segments.size(); i++) {
                const bool isSelected = (int)id == i;
                auto itemLabel = std::to_string(i);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    changed = true;
                    id = (SegID)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    inline void TexturePreview(LevelTexID tid, const ImVec2& size = { 64.0f, 64.0f }) {
        if (tid == LevelTexID::None) return;
        auto& material = Render::Materials->Get(tid);
        ImGui::Image((ImTextureID)material.Pointer(), size);
    }

    inline bool SideDropdown(SideID& id) {
        ImGui::SetNextItemWidth(-1);
        bool changed = false;

        auto label = std::to_string((int)id);
        if (ImGui::BeginCombo("##sides", label.c_str())) {
            for (int i = 0; i < 6; i++) {
                const bool isSelected = (int)id == i;
                auto itemLabel = std::to_string((int)i);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    changed = true;
                    id = (SideID)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    class PropertyEditor : public WindowBase {
        enum class ActivePanel { Segment, Texture, Object };
        Option<Tag> _selectedReactorTrigger;
        MatcenEditor _matcenEditor;
        bool _loaded = false;
    public:
        PropertyEditor() : WindowBase("Properties", &Settings::Editor.Windows.Properties) { }

    protected:
        void OnUpdate() override {
            // Header
            if (Settings::Editor.SelectionMode == SelectionMode::Object) {
                if (!Game::Level.TryGetObject(Selection.Object)) {
                    ImGui::Text("No object is selected");
                    return;
                }
            }
            else {
                if (!Game::Level.SegmentExists(Selection.Segment)) {
                    ImGui::Text("No segment is selected");
                    return;
                }
            }

            constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("properties", 2, flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                // Body
                if (Settings::Editor.SelectionMode == SelectionMode::Object) {
                    ObjectProperties();
                }
                else {
                    SegmentProperties();
                }

                ImGui::EndTable();
            }

            _matcenEditor.Update();
        }

    private:
        void SegmentProperties();
        void ObjectProperties() const;
    };
}
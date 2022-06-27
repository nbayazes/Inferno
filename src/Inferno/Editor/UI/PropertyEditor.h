#pragma once

#include <map>

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
                auto itemLabel = std::to_string((int)i);
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
        ImGui::Image((ImTextureID)material.Handles[Render::Material2D::Diffuse].ptr, size);
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
        PropertyEditor() : WindowBase("Properties", &Settings::Windows.Properties) { }

    protected:
        void OnUpdate() override {
            // Header
            if (Settings::SelectionMode == SelectionMode::Object) {
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

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::Columns(2);
            if (!_loaded) {
                ImGui::SetColumnWidth(0, 175);
                _loaded = true;
            }
            //ImGui::Separator();

            // Body
            if (Settings::SelectionMode == SelectionMode::Object) {
                ObjectProperties();
            }
            else {
                SegmentProperties();
            }

            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::PopStyleVar();

            _matcenEditor.Update();
        }

    private:
        void SegmentProperties();
        void ObjectProperties();
    };
}
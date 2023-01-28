#pragma once
#include "Game.h"
#include "WindowBase.h"
#include "Editor/Editor.Selection.h"
#include "Editor/Gizmo.h"

namespace Inferno::Editor {
    class ScaleWindow final : public WindowBase {
        Vector3 _scale = { 1, 1, 1 };
        bool _uniform = false;

    public:
        ScaleWindow() : WindowBase("Scale", &Settings::Editor.Windows.Scale) {}

        void OnUpdate() override {
            const float labelWidth = 30 * Shell::DpiScale;
            ImGui::TextColored({ 1, 0.4, 0.4, 1 }, "X");
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##X", &_scale.x, 0.1, 0, "%.2f") && _uniform)
                _scale.y = _scale.z = _scale.x;

            {
                DisableControls disable(_uniform);
                ImGui::TextColored({ 0.4, 1, 0.4, 1 }, "Y");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputFloat("##Y", &_scale.y, 0.1, 0, "%.2f") && _uniform)
                    _scale.x = _scale.z = _scale.y;

                ImGui::TextColored({ 0.4, 0.4, 1, 1 }, "Z");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputFloat("##Z", &_scale.z, 0.1, 0, "%.2f") && _uniform)
                    _scale.x = _scale.y = _scale.z;
            }
            ImGui::Dummy({ 0, 5 });

            if (ImGui::Checkbox("Uniform", &_uniform) && _uniform)
                _scale.z = _scale.y = _scale.x;

            ImGui::Dummy({ 0, 10 });
            if (ImGui::Button("Apply Scale"))
                ApplyScale();
        }

        void ApplyScale() const {
            Editor::History.SnapshotSelection();
            auto gizmo = Editor::Gizmo.Transform;
            auto scale = Matrix::CreateScale(_scale.z, _scale.y, _scale.x);

            // Rotate scale to match gizmo
            auto transform = gizmo.Invert() * scale * gizmo;

            // transform the vertices
            auto vertices = Editor::Marked.GetVertexHandles(Game::Level);
            for (auto& tag : vertices) {
                auto& v = Game::Level.Vertices[tag];
                v = Vector3::Transform(v, transform);
            }

            // Scale object positions in segment mode
            if (Settings::Editor.SelectionMode == SelectionMode::Segment) {
                for (auto& obj : Game::Level.Objects) {
                    if (Editor::Marked.Segments.contains(obj.Segment)) {
                        obj.Position = Vector3::Transform(obj.Position, transform);
                    }
                }
            }

            Editor::History.SnapshotLevel("Scale");
            Editor::Events::LevelChanged();
        }
    };
}

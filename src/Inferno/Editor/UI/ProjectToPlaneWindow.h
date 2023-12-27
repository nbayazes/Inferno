#pragma once

namespace Inferno::Editor {
    struct ProjectionAxisArgs {
        Vector3 Axis;
        Option<Vector3> DrawLocation;

        bool IsValid() const {
            return Axis != Vector3::Zero && DrawLocation;
        }
    };

    class ProjectToPlaneWindow final : public WindowBase {
    public:
        inline static ProjectionAxisArgs Args;

        ProjectToPlaneWindow() : WindowBase("Project Geometry to Plane", &Settings::Editor.Windows.ProjectToPlane) {
            // There isn't much in this window, so make it a little shorter
            DefaultHeight = 200 * Shell::DpiScale;
        }

        void OnUpdate() override {
            ImGui::Text("Use gizmo axis");

            //if (ImGui::Button("Use edge")) {
            //    auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());
            //    Args.DrawLocation = face.Center();
            //    Args.Axis = face.VectorForEdge(Editor::Selection.Point);
            //}

            ImGui::SameLine();
            constexpr ImVec2 size(30, 0);

            if (ImGui::Button("R", size)) {
                Args.DrawLocation = Editor::Gizmo.Transform.Translation();
                Args.Axis = Editor::Gizmo.Transform.Forward();
            }

            ImGui::SameLine();
            if (ImGui::Button("G", size)) {
                Args.DrawLocation = Editor::Gizmo.Transform.Translation();
                Args.Axis = Editor::Gizmo.Transform.Up();
            }

            ImGui::SameLine();
            if (ImGui::Button("B", size)) {
                Args.DrawLocation = Editor::Gizmo.Transform.Translation();
                Args.Axis = Editor::Gizmo.Transform.Right();
            }
            //ImGui::SameLine();

            //{
            //    // Typing in a vector isn't straight forward due to normalization. Revisit later if requested.
            //    DisableControls disable(true);
            //    ImGui::SetNextItemWidth(-1);
            //    ImGui::InputFloat3("##Axis", &Args.Axis.x);
            //}

            //ImGui::Dummy({ 0, 20 });

            {
                DisableControls disable(!Args.IsValid());
                if (ImGui::Button("Project geometry", { 150, 0 }))
                    Project();

                ImGui::HelpMarker("Projects marked geometry along the picked axis\nto the plane of the selected face");
            }
        }

    private:
        static void Project() {
            Editor::History.SnapshotSelection();
            auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());

            for (auto& index : Editor::GetSelectedVertices()) {
                auto& v = Game::Level.Vertices[index];
                // We're using the average plane of the selected face, not one of the two triangles.
                v = ProjectRayOntoPlane(Ray(v, Args.Axis), face.Center(), face.AverageNormal());
            }

            Game::Level.UpdateAllGeometricProps();
            Editor::History.SnapshotLevel("Project Geometry to Plane");
            Editor::Events::LevelChanged();
        }
    };
}

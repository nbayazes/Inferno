#pragma once

namespace Inferno::Editor {
    struct ProjectionAxisArgs {
        Vector3 Axis;
        Vector3 DrawLocation;
        bool DrawLocationSet;

        bool IsValid() const {
            // DrawLocation isn't *technically* necessary to project,
            // but it's easier to handle both things at once.
            return DrawLocationSet && Axis.LengthSquared() > 0;
        }
    };
    inline ProjectionAxisArgs SnapToPlaneArgs;

    class SnapToPlaneWindow final : public WindowBase {
        float _axis[3];

    public:
        SnapToPlaneWindow() : WindowBase("Snap Points to Plane", &Settings::Editor.Windows.SnapToPlane) {
            // There isn't much in this window, so make it a little shorter
            DefaultHeight = 200 * Shell::DpiScale;
        }

        void OnUpdate() override {
            auto& args = SnapToPlaneArgs;

            if (ImGui::Button("Pick Projection Axis")) {
                auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());
                auto normal = face.VectorForEdge(Editor::Selection.Point);
                _axis[0] = normal.x;
                _axis[1] = normal.y;
                _axis[2] = normal.z;
                args.DrawLocation = face.Center();
                args.DrawLocationSet = true;
                args.Axis = normal;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat3("##Axis", _axis)) {
                Vector3 normalizedAxis = Vector3(_axis[0], _axis[1], _axis[2]);
                normalizedAxis.Normalize();
                args.Axis = normalizedAxis;
                if (!args.DrawLocationSet) {
                    args.DrawLocation = Face::FromSide(Game::Level, Editor::Selection.Tag()).Center();
                    args.DrawLocationSet = true;
                }
            }

            ImGui::Dummy({ 0, 20 });

            {
                DisableControls disable(!args.IsValid());
                if (ImGui::Button("Project", { 100, 0 }))
                    Project();
                ImGui::HelpMarker("Projects marked points onto the plane defined by the current face.");
            }
        }

        static void Project() {
            auto& args = SnapToPlaneArgs;
            auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());
            auto vertices = Editor::GetSelectedVertices();

            for (auto& tag : vertices) {
                auto& v = Game::Level.Vertices[tag];
                // We're using the average plane of the selected face, not one of the two triangles.
                v = ProjectRayOntoPlane(Ray(v, args.Axis), face.Center(), face.AverageNormal());
            }

            Game::Level.UpdateAllGeometricProps();
            Editor::History.SnapshotLevel("Snap Points to Plane");
            Editor::Events::LevelChanged();
        }
    };
}

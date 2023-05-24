#include "pch.h"
#include "Editor.h"
#include "Types.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {
    void UpdateCamera(Camera& camera) {
        auto& delta = Input::MouseDelta;
        auto& wheelDelta = Input::WheelDelta;

        if (Input::GetMouseMode() == Input::MouseMode::Mouselook) {
            int inv = Settings::Editor.InvertY ? 1 : -1;
            camera.Rotate(delta.x * Settings::Editor.MouselookSensitivity, delta.y * inv * Settings::Editor.MouselookSensitivity);
        }

        if (Input::GetMouseMode() == Input::MouseMode::Orbit) {
            if (Input::AltDown) {
                camera.Pan(-delta.x * Settings::Editor.MoveSpeed * 0.001f, -delta.y * Settings::Editor.MoveSpeed * 0.001f);
            }
            else {
                int inv = Settings::Editor.InvertOrbitY ? 1 : -1;
                camera.Orbit(-delta.x * Settings::Editor.MouselookSensitivity, delta.y * inv * Settings::Editor.MouselookSensitivity);
            }
        }

        if (wheelDelta < 0) camera.ZoomIn();
        if (wheelDelta > 0) camera.ZoomOut();
    }

    void ZoomExtents(const Level& level, Camera& camera) {
        Vector3 min, max, target;

        for (auto& v : level.Vertices) {
            if (v.x < min.x) min.x = v.x;
            if (v.y < min.y) min.y = v.y;
            if (v.z < min.z) min.z = v.z;

            if (v.x > max.x) max.x = v.x;
            if (v.y > max.y) max.y = v.y;
            if (v.z > max.z) max.z = v.z;

            target += v;
        }

        if (level.Vertices.empty()) return;
        target /= (float)level.Vertices.size();

        auto pos = max;
        Vector3 dir = target - pos;
        dir.Normalize();

        auto right = dir.Cross(Vector3::Up);
        camera.Up = right.Cross(dir);
        camera.Target = pos + dir * 60;
        camera.Position = pos - dir * 20;
    }

    void AlignViewToFace(Level& level, Camera& camera, Tag tag, int point) {
        if (!level.SegmentExists(tag)) return;
        auto face = Face::FromSide(level, tag);

        camera.Target = face.Center();
        camera.Position = face.Center() + face.AverageNormal() * std::sqrt(face.Area()) * 1.25;
        camera.Up = face.VectorForEdge(point).Cross(-face.AverageNormal());
    }

    namespace Commands {
        Command FocusSegment{
            .Action = [] {
                if (auto seg = Game::Level.TryGetSegment(Selection.Segment))
                    Render::Camera.MoveTo(seg->Center);
            },
            .Name = "Focus Segment"
        };

        Command FocusObject{
            .Action = [] {
                if (auto obj = Game::Level.TryGetObject(Selection.Object))
                    Render::Camera.MoveTo(obj->Position);
            },
            .Name = "Focus Object"
        };

        Command FocusSelection{
            .Action = [] { Render::Camera.MoveTo(Editor::Gizmo.Transform.Translation()); },
            .Name = "Focus Selection"
        };
    }
}

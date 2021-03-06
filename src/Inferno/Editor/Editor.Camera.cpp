#include "pch.h"
#include "Editor.h"
#include "Types.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {

    void UpdateCamera(Camera& camera) {
        auto& delta = Input::MouseDelta;
        auto& wheelDelta = Input::WheelDelta;

        if (Input::GetMouselook()) {
            int inv = Settings::InvertY ? 1 : -1;
            camera.Rotate(delta.x * Settings::MouselookSensitivity, delta.y * inv * Settings::MouselookSensitivity);
        }

        if (wheelDelta < 0) camera.ZoomIn();
        if (wheelDelta > 0) camera.ZoomOut();
    }

    void Commands::FocusSegment() {
        if (auto seg = Game::Level.TryGetSegment(Selection.Segment)) {
            Render::Camera.MoveTo(seg->Center);
        }
    }

    void Commands::FocusObject() {
        if (auto obj = Game::Level.TryGetObject(Selection.Object))
            Render::Camera.MoveTo(obj->Position);
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
}
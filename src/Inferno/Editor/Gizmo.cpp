#include "pch.h"
#include "Gizmo.h"
#include "Settings.h"
#include "Editor.h"
#include "Input.h"

namespace Inferno::Editor {
    using namespace DirectX::SimpleMath;
    using DirectX::BoundingOrientedBox;

    BoundingOrientedBox GetGizmoBoundingBox(const Vector3& position, const Vector3& direction, float scale) {
        BoundingOrientedBox bounds(
            { Settings::GizmoSize, 0, 0 },
            { Settings::GizmoSize, Settings::GizmoThickness * 2, Settings::GizmoThickness * 2 },
            Vector4::UnitW);
        bounds.Transform(bounds, Matrix::CreateScale(scale) * DirectionToRotationMatrix(direction) * Matrix::CreateTranslation(position));
        return bounds;
    }

    struct Hit {
        GizmoAxis Axis;
        float Distance = std::numeric_limits<float>::max();
        TransformMode Transform;
    };

    Vector3 GetPlaneNormal(GizmoAxis axis, const Matrix& transform) {
        switch (axis) {
            default:
            case GizmoAxis::X: return transform.Forward();
            case GizmoAxis::Y: return transform.Up();
            case GizmoAxis::Z: return transform.Right();
        }
    }

    Hit IntersectTranslation(const Matrix& transform, const Ray& ray, Array<bool, 3>& enabled, const Camera& camera) {
        auto position = transform.Translation();
        auto scale = GetGizmoScale(position, camera);
        auto xAxis = transform.Forward(), yAxis = transform.Up(), zAxis = transform.Right();
        auto xBounds = GetGizmoBoundingBox(position, xAxis, scale);
        auto yBounds = GetGizmoBoundingBox(position, yAxis, scale);
        auto zBounds = GetGizmoBoundingBox(position, zAxis, scale);

        auto gizmoDir = camera.Position - position;
        gizmoDir.Normalize();

        Hit hits[3]{};
        float dist{};
        if (enabled[0]
            && std::abs(xAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && xBounds.Intersects(ray.position, ray.direction, dist))
            hits[0] = { GizmoAxis::X, dist, TransformMode::Translation };

        if (enabled[1]
            && std::abs(yAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && yBounds.Intersects(ray.position, ray.direction, dist))
            hits[1] = { GizmoAxis::Y, dist, TransformMode::Translation };

        if (enabled[2]
            && std::abs(zAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && zBounds.Intersects(ray.position, ray.direction, dist))
            hits[2] = { GizmoAxis::Z, dist, TransformMode::Translation };

        Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });
        return hits[0];
    }

    Hit IntersectRotation(const Matrix& transform, const Ray& ray, Array<bool, 3>& enabled, const Camera& camera) {
        auto position = transform.Translation();
        auto scale = GetGizmoScale(position, camera);

        Plane xPlane(position, transform.Forward());
        Plane yPlane(position, transform.Up());
        Plane zPlane(position, transform.Right());

        auto gizmoDir = camera.Position - position;
        gizmoDir.Normalize();

        // Hit test each rotation plane
        Hit hits[3]{};
        float dist{};
        if (enabled[0] 
            && std::abs(transform.Forward().Dot(gizmoDir)) >= 1 - TransformGizmo::MaxViewAngle
            && ray.Intersects(xPlane, dist))
            hits[0] = { GizmoAxis::X, dist, TransformMode::Rotation };

        if (enabled[1] 
            && std::abs(transform.Up().Dot(gizmoDir)) >= 1 - TransformGizmo::MaxViewAngle
            && ray.Intersects(yPlane, dist))
            hits[1] = { GizmoAxis::Y, dist, TransformMode::Rotation };

        if (enabled[2] 
            && std::abs(transform.Right().Dot(gizmoDir)) >= 1 - TransformGizmo::MaxViewAngle
            && ray.Intersects(zPlane, dist))
            hits[2] = { GizmoAxis::Z, dist, TransformMode::Rotation };

        Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });

        // Check if any intersections lie on the gizmo circle
        for (auto& hit : hits) {
            if (hit.Axis == GizmoAxis::None) continue;
            auto intersection = ray.direction * hit.Distance;
            auto distance = Vector3::Distance(intersection, position - camera.Position);

            if (distance > Settings::GizmoSize * 0.8 * scale &&
                distance < Settings::GizmoSize * 1.2 * scale)
                return hit;
        }

        return {};
    }

    Hit IntersectScale(const Matrix& transform, const Ray& ray, Array<bool, 3>& enabled, const Camera& camera) {
        auto GetBoundingBox = [](const Vector3& position, const Vector3& direction, float scale) {
            BoundingOrientedBox bounds({ 0, 0, 0 }, { 1, 1, 1 }, Vector4::UnitW);
            auto translation = Matrix::CreateTranslation(position + direction * Settings::GizmoSize * scale);
            bounds.Transform(bounds, Matrix::CreateScale(scale) * DirectionToRotationMatrix(direction) * translation);
            return bounds;
        };

        auto position = transform.Translation();
        auto scale = GetGizmoScale(position, camera);
        auto xAxis = transform.Forward(), yAxis = transform.Up(), zAxis = transform.Right();
        auto xBounds = GetBoundingBox(position, xAxis, scale);
        auto yBounds = GetBoundingBox(position, yAxis, scale);
        auto zBounds = GetBoundingBox(position, zAxis, scale);

        auto gizmoDir = camera.Position - position;
        gizmoDir.Normalize();

        Hit hits[3]{};
        float dist{};
        if (enabled[0]
            && std::abs(xAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && xBounds.Intersects(ray.position, ray.direction, dist))
            hits[0] = { GizmoAxis::X, dist, TransformMode::Scale };

        if (enabled[1]
            && std::abs(yAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && yBounds.Intersects(ray.position, ray.direction, dist))
            hits[1] = { GizmoAxis::Y, dist, TransformMode::Scale };

        if (enabled[2]
            && std::abs(zAxis.Dot(gizmoDir)) <= TransformGizmo::MaxViewAngle
            && zBounds.Intersects(ray.position, ray.direction, dist))
            hits[2] = { GizmoAxis::Z, dist, TransformMode::Scale };

        Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });
        return hits[0];
    }

    Matrix GetGizmoTransform(Level& level, const TransformGizmo& gizmo) {
        Matrix transform;

        if (Settings::EnableTextureMode) {
            if (gizmo.State == GizmoState::Dragging) {
                transform = Editor::Gizmo.Transform; // no change;
            }
            else {
                if (!level.SegmentExists(Selection.Segment)) return transform;
                auto face = Face::FromSide(level, Selection.Tag());
                auto normal = face.AverageNormal();
                auto tangent = face.VectorForEdge(Selection.Point);
                auto bitangent = normal.Cross(tangent);

                transform.Forward(bitangent);
                transform.Up(tangent);
                transform.Right(normal);
                transform.Translation(face[Selection.Point]);
            }
            return transform;
        }

        if (Settings::SelectionMode == SelectionMode::Transform) {
            transform = UserCSys;
        }
        else if (Settings::CoordinateSystem == CoordinateSystem::User) {
            transform = UserCSys;

            // Move translation gizmo to the object even in global mode for clarity
            // Consider always doing this and drawing a line or arc to the reference?
            //if (Settings::ActiveTransform == TransformMode::Translation)
            //transform.Translation(Editor::Selection.GetOrigin());
        }
        else if (Settings::SelectionMode == SelectionMode::Object &&
                 Selection.Object != ObjID::None) {
            // use object orientation
            if (auto obj = level.TryGetObject(Selection.Object)) {
                transform = obj->GetTransform();
                transform.Forward(obj->Rotation.Forward());
                transform.Translation(Editor::Selection.GetOrigin(Settings::SelectionMode));
            }
        }
        else if (level.SegmentExists(Selection.Segment)) {
            transform = GetTransformFromSelection(level, Selection.Tag(), Settings::SelectionMode);
        }

        if (Settings::CoordinateSystem == CoordinateSystem::Global) {
            // global overrides the rotatation to the XYZ axis
            transform.Right(Vector3::UnitX);
            transform.Up(Vector3::UnitY);
            transform.Forward(Vector3::UnitZ);
        }

        return transform;
    }

    void TransformGizmo::UpdatePosition() {
        Transform = GetGizmoTransform(Game::Level, *this);
    }

    void TransformGizmo::UpdateAxisVisiblity(SelectionMode mode) {
        if (Settings::EnableTextureMode) {
            switch (mode) {
                case Inferno::Editor::SelectionMode::Segment:
                case Inferno::Editor::SelectionMode::Face:
                    ShowTranslationAxis = { true, true, false };
                    ShowRotationAxis = { false, false, true };
                    ShowScaleAxis = { true, true, false };
                    break;
                case Inferno::Editor::SelectionMode::Edge:
                    ShowTranslationAxis = { true, true, false };
                    ShowRotationAxis = { false, false, false };
                    ShowScaleAxis = { false, false, false };
                    break;
                case Inferno::Editor::SelectionMode::Point:
                    ShowTranslationAxis = { true, true, false };
                    ShowRotationAxis = { false, false, false };
                    ShowScaleAxis = { false, false, false };
                    break;
                default:
                    break;
            }
        }
        else {
            switch (mode) {
                case SelectionMode::Object:
                    ShowTranslationAxis = { true, true, true };
                    ShowRotationAxis = { true, true, true };
                    ShowScaleAxis = { false, false, false };
                    break;
                default:
                    ShowTranslationAxis = { true, true, true };
                    ShowRotationAxis = { true, true, true };
                    ShowScaleAxis = { true, true, true };
                    break;
            }
        }
    }

    void TransformGizmo::UpdateDrag(const Camera& camera) {
        switch (Mode) {
            case TransformMode::Translation:
            {
                auto end = ProjectRayOntoPlane(MouseRay, StartTransform.Translation(), camera.GetForward());
                auto delta = end - CursorStart;
                auto magnitude = std::min(delta.Dot(Direction), 10000.0f);
                auto translation = Step(magnitude, Settings::TranslationSnap) * Direction;
                auto deltaTranslation = translation - _prevTranslation;
                DeltaTransform = Matrix::CreateTranslation(deltaTranslation);
                auto sign = Direction.Dot(DeltaTransform.Translation()) > 0 ? 1 : -1;
                Delta = (deltaTranslation).Length() * sign;
                TotalDelta += Delta;
                _prevTranslation = translation;
                break;
            }
            case TransformMode::Scale:
            {
                auto end = ProjectRayOntoPlane(MouseRay, StartTransform.Translation(), camera.GetForward());
                auto delta = end - CursorStart;
                auto magnitude = std::min(delta.Dot(Direction), 10000.0f);
                auto translation = Step(magnitude, Settings::TranslationSnap) * Direction;
                DeltaTransform.Translation(translation - _prevTranslation);
                Grow = Direction.Dot(DeltaTransform.Translation()) > 0;
                Delta = (translation - _prevTranslation).Length() * (Grow ? 1 : -1);
                TotalDelta += Delta;
                _prevTranslation = translation;
                break;
            }
            case TransformMode::Rotation:
            {
                auto normal = camera.GetForward(); // use the camera as the rotation plane
                auto planeNormal = GetPlaneNormal(SelectedAxis, StartTransform);

                auto position = StartTransform.Translation();
                auto end = ProjectRayOntoPlane(MouseRay, position, normal);
                float angle = AngleBetweenPoints(CursorStart, end, position, normal);
                angle = Step(angle, Settings::RotationSnap);
                if (normal.Dot(planeNormal) < 0)
                    angle = -angle;

                Delta = angle - _prevAngle;
                TotalDelta += Delta;

                DeltaTransform = Matrix::CreateTranslation(-position) * Matrix::CreateFromAxisAngle(planeNormal, Delta) * Matrix::CreateTranslation(position);
                _prevAngle = angle;
                break;
            }
        }
    }

    void TransformGizmo::Update(Input::SelectionState state, const Camera& camera) {
        switch (state) {
            case Input::SelectionState::None:
            {
                State = GizmoState::None;
                if (Settings::SelectionMode == SelectionMode::Object &&
                    !Game::Level.TryGetObject(Editor::Selection.Object))
                    return; // valid object not selected, don't hit test gizmo

                Hit hits[3]{};
                hits[0] = IntersectTranslation(Transform, MouseRay, ShowTranslationAxis, camera);
                hits[1] = IntersectRotation(Transform, MouseRay, ShowRotationAxis, camera);
                hits[2] = IntersectScale(Transform, MouseRay, ShowScaleAxis, camera);
                Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });

                Mode = hits[0].Transform;
                SelectedAxis = hits[0].Axis;
                break;
            }
            case Input::SelectionState::Preselect:
                if (SelectedAxis == GizmoAxis::None) return; // User didn't click the gizmo
                Direction = GetPlaneNormal(SelectedAxis, Transform);
                CursorStart = ProjectRayOntoPlane(MouseRay, Transform.Translation(), camera.GetForward());
                StartTransform = Transform;
                _prevAngle = 0;
                break;

            case Input::SelectionState::BeginDrag:
                if (SelectedAxis == GizmoAxis::None) return;

                if (State == GizmoState::None)
                    State = GizmoState::BeginDrag;

                DeltaTransform = Matrix::Identity;
                _prevTranslation = {};
                Delta = TotalDelta = 0;
                break;

            case Input::SelectionState::Dragging:
                if (State == GizmoState::BeginDrag)
                    State = GizmoState::Dragging;

                UpdateDrag(camera);
                break;

            case Input::SelectionState::Released:
                if (SelectedAxis == GizmoAxis::None) return;

                // clicked an axis
                State = Input::LeftDragState == Input::SelectionState::Released ? GizmoState::LeftClick : GizmoState::RightClick;
                break;

            case Input::SelectionState::ReleasedDrag:
                if (State == GizmoState::Dragging)
                    State = GizmoState::EndDrag;

                SelectedAxis = GizmoAxis::None;
                break;
        }
    }

    float GetGizmoScale(const Vector3& position, const Camera& camera) {
        auto target = position - camera.Position;
        auto right = camera.GetRight();
        // project the target onto the camera plane so panning does not cause scaling.
        auto projection = target.Dot(right) * right;
        auto distance = (target - projection).Length();
        return distance / 40.0f;
    }

    Matrix GetTransformFromSide(Level& level, Tag tag, int point) {
        Matrix transform;
        auto face = Face::FromSide(level, tag);
        auto normal = face.AverageNormal();
        auto tangent = face.VectorForEdge(point % 4);

        transform.Forward(normal);
        transform.Up(tangent);
        auto bitangent = normal.Cross(tangent);
        bitangent.Normalize();
        transform.Right(bitangent);
        return transform;
    }

    Matrix GetTransformFromSelection(Level& level, Tag tag, SelectionMode mode) {
        Matrix transform;

        if (level.SegmentExists(tag.Segment)) {
            transform = GetTransformFromSide(level, tag, Editor::Selection.Point);
            transform.Translation(Editor::Selection.GetOrigin(mode));
        }

        return transform;
    }
}
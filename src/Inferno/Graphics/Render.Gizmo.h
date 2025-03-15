#pragma once

#include "Render.Debug.h"
#include <DirectXMath.h>
#include "Editor/Gizmo.h"
#include "CameraContext.h"
#include "Colors.h"

namespace Inferno::Render {

    inline Color GetColor(Editor::GizmoAxis axis, const Editor::TransformGizmo& gizmo, Editor::TransformMode mode) {
        if (Input::GetMouseMode() != Input::MouseMode::Normal) return Colors::Disabled;

        bool highlight = gizmo.SelectedAxis == axis && gizmo.Mode == mode;
        switch (axis) {
            default:
            case Editor::GizmoAxis::X:
                return highlight ? Colors::GizmoXHighlight : Colors::GizmoX;
            case Editor::GizmoAxis::Y:
                return highlight ? Colors::GizmoYHighlight : Colors::GizmoY;
            case Editor::GizmoAxis::Z:
                return highlight ? Colors::GizmoZHighlight : Colors::GizmoZ;
        }
    }

    inline void DrawGizmoPreview(const Editor::TransformGizmo& gizmo, const Camera& camera) {
        using namespace Editor;
        if (gizmo.SelectedAxis == GizmoAxis::None) return;

        auto color = [&gizmo] {
            switch (gizmo.SelectedAxis) {
                default:
                case GizmoAxis::X: return Colors::GizmoXHighlight;
                case GizmoAxis::Y: return Colors::GizmoYHighlight;
                case GizmoAxis::Z: return Colors::GizmoZHighlight;
            }
        }();

        if (gizmo.Mode == TransformMode::Translation)
            Debug::DrawLine(GizmoPreview::Start, GizmoPreview::End, color);

        auto position = gizmo.Transform.Translation();
        auto gizmoScale = Editor::GetGizmoScale(position, camera);

        if (gizmo.Mode == TransformMode::Rotation) {
            auto scale = Matrix::CreateScale(gizmoScale);
            auto translation = Matrix::CreateTranslation(position);

            Debug::DrawLine(GizmoPreview::Start, GizmoPreview::End, color);
            auto rvec = GizmoPreview::RotationStart - position;
            rvec.Normalize();

            auto gizmoDir = camera.Position - position;
            gizmoDir.Normalize();

            auto drawAxis = [&](GizmoAxis axis, const Vector3& normal) {
                //auto target = ProjectPointOntoPlane(Camera.Position, position, normal);
                auto rotation = Matrix::CreateRotationY(DirectX::XM_PIDIV2) * DirectionToRotationMatrix(normal);
                auto transform = rotation * scale * translation;
                auto axisColor = GetColor(axis, gizmo, Editor::TransformMode::Rotation);
                Plane plane(position, transform.Forward());

                auto& ray = Editor::MouseRay;
                float dist{};
                if (ray.Intersects(plane, dist)) {
                    auto ivec = ray.position + ray.direction * dist - position;
                    ivec.Normalize();

                    auto rotationEnd = position + ivec * Settings::Editor.GizmoSize * gizmoScale;
                    Debug::DrawLine(position, GizmoPreview::RotationStart, axisColor);
                    Debug::DrawLine(position, rotationEnd, axisColor);
                }

                Debug::DrawRing(Settings::Editor.GizmoSize, 0.25f, transform, axisColor);
            };

            switch (gizmo.SelectedAxis) {
                case GizmoAxis::X: return drawAxis(GizmoAxis::X, gizmo.Transform.Forward());
                case GizmoAxis::Y: return drawAxis(GizmoAxis::Y, gizmo.Transform.Up());
                case GizmoAxis::Z: return drawAxis(GizmoAxis::Z, gizmo.Transform.Right());
            }
        }

        if (gizmo.Mode == TransformMode::Scale) {
            auto [up, right] = [&gizmo]() -> Tuple<Vector3, Vector3> {
                switch (gizmo.SelectedAxis) {
                    default:
                    case GizmoAxis::X: return { gizmo.Transform.Up(), gizmo.Transform.Right() };
                    case GizmoAxis::Y: return { gizmo.Transform.Forward(), gizmo.Transform.Right() };
                    case GizmoAxis::Z: return { gizmo.Transform.Up(), gizmo.Transform.Forward() };
                }
            }();

            Debug::DrawLine(GizmoPreview::Start, GizmoPreview::End, color);
            Debug::DrawPlane(position, right, up, color, gizmoScale * 10);
        }
    }

    inline void DrawTranslationGizmo(const GraphicsContext& ctx, const Editor::TransformGizmo& gizmo) {
        using namespace Editor;
        auto sizeScale = Settings::Editor.GizmoSize / 5.0f; // arrows have a default size of 5
        auto position = gizmo.Transform.Translation();
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(position, ctx.Camera) * sizeScale);
        auto translation = Matrix::CreateTranslation(position);

        auto gizmoDir = ctx.Camera.Position - position;
        gizmoDir.Normalize();

        auto drawAxis = [&](GizmoAxis axis, Vector3 dir) {
            if (std::abs(dir.Dot(gizmoDir)) > TransformGizmo::MaxViewAngle)
                return; // Hide gizmo if camera is aligned to it

            auto rotation = DirectionToRotationMatrix(dir);
            auto transform = rotation * scale * translation * ctx.Camera.ViewProjection;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Translation);
            Debug::DrawArrow(ctx.GetCommandList(), transform, color);
        };

        if (gizmo.ShowTranslationAxis[0]) drawAxis(GizmoAxis::X, gizmo.Transform.Forward());
        if (gizmo.ShowTranslationAxis[1]) drawAxis(GizmoAxis::Y, gizmo.Transform.Up());
        if (gizmo.ShowTranslationAxis[2]) drawAxis(GizmoAxis::Z, gizmo.Transform.Right());
    }

    inline void DrawRotationGizmo(const Editor::TransformGizmo& gizmo, const Camera& camera) {
        using namespace Editor;
        auto position = gizmo.Transform.Translation();
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(position, camera));
        auto translation = Matrix::CreateTranslation(position);

        auto gizmoDir = camera.Position - position;
        gizmoDir.Normalize();

        auto DrawAxis = [&](GizmoAxis axis, const Vector3& normal, const Vector3& orient) {
            auto cdot = normal.Dot(gizmoDir);
            if (std::abs(cdot) < 1 - TransformGizmo::MaxViewAngle) return; // Don't draw axis at sharp angles

            auto rotation = Matrix::CreateRotationY(DirectX::XM_PIDIV2) * DirectionToRotationMatrix(normal);
            auto transform = rotation * scale * translation;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Rotation);

            if (std::abs(cdot) > TransformGizmo::MaxViewAngle) {// Draw solid ring if camera is looking directly at circle
                Debug::DrawRing(Settings::Editor.GizmoSize, 0.25f, transform, color);
            }
            else {
                // DrawArc() draws on the XY axis, rotate it by 90 on Y to align to XZ axis.
                auto target = ProjectPointOntoPlane(camera.Position, position, normal);
                auto cameraDir = target - position; // direction towards the camera on this plane
                cameraDir.Normalize();
                auto cameraAngle = AngleBetweenVectors(orient, cameraDir, normal); // angle between the camera on this plane and the ref
                Vector3 arcRef = Vector3::Transform(Vector3::UnitY, rotation); // rotate the arc center ref with it
                cameraAngle += AngleBetweenVectors(arcRef, orient, normal); // center the arc on the orientation vector
                Debug::DrawSolidArc(Settings::Editor.GizmoSize, 0.25f,
                                    180 * DegToRad, // length
                                    cameraAngle, // offset
                                    transform, color);
            }
        };

        if (gizmo.ShowRotationAxis[0]) DrawAxis(GizmoAxis::X, gizmo.Transform.Forward(), gizmo.Transform.Right());
        if (gizmo.ShowRotationAxis[1]) DrawAxis(GizmoAxis::Y, gizmo.Transform.Up(), gizmo.Transform.Forward());
        if (gizmo.ShowRotationAxis[2]) DrawAxis(GizmoAxis::Z, gizmo.Transform.Right(), gizmo.Transform.Up());
    }

    inline void DrawScaleGizmo(const GraphicsContext& ctx, const Editor::TransformGizmo& gizmo) {
        using namespace Editor;
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(gizmo.Transform.Translation(), ctx.Camera));
        auto translation = Matrix::CreateTranslation(gizmo.Transform.Translation());

        auto gizmoDir = ctx.Camera.Position - gizmo.Transform.Translation();
        gizmoDir.Normalize();

        auto drawAxis = [&](GizmoAxis axis) {
            auto rotation = gizmo.Transform;
            rotation.Translation(Vector3::Zero);

            if (axis == GizmoAxis::Y) {
                rotation.Forward(gizmo.Transform.Up());
                rotation.Up(-gizmo.Transform.Forward());
            }

            if (axis == GizmoAxis::Z) {
                rotation.Forward(gizmo.Transform.Right());
                rotation.Right(-gizmo.Transform.Forward());
            }

            if (std::abs(rotation.Forward().Dot(gizmoDir)) > TransformGizmo::MaxViewAngle)
                return; // Hide gizmo if camera is aligned to it

            auto offset = Matrix::CreateTranslation(rotation.Forward() * Settings::Editor.GizmoSize);
            auto transform = rotation * offset * scale * translation * ctx.Camera.ViewProjection;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Scale);
            Debug::DrawCube(ctx.GetCommandList(), transform, color);
        };

        if (gizmo.ShowScaleAxis[0]) drawAxis(GizmoAxis::X);
        if (gizmo.ShowScaleAxis[1]) drawAxis(GizmoAxis::Y);
        if (gizmo.ShowScaleAxis[2]) drawAxis(GizmoAxis::Z);
    }
}
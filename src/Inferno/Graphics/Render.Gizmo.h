#pragma once

#include "Render.Debug.h"
#include <DirectXMath.h>
#include "Editor/Gizmo.h"

namespace Inferno::Render {
    namespace Colors {
        // Blender colors. Need to be thicker to be visible
        //constexpr Color GizmoX = ColorFromRGB(217, 100, 111);
        //constexpr Color GizmoXHighlight = ColorFromRGB(255, 54, 83);
        //constexpr Color GizmoY = ColorFromRGB(121, 180, 57);
        //constexpr Color GizmoYHighlight = ColorFromRGB(138, 219, 0);
        //constexpr Color GizmoZ = ColorFromRGB(66, 124, 207);
        //constexpr Color GizmoZHighlight = ColorFromRGB(44, 143, 255);

        constexpr Color GizmoX = ColorFromRGB(244, 100, 111);
        constexpr Color GizmoXHighlight = ColorFromRGB(255, 200, 180);
        constexpr Color GizmoY = ColorFromRGB(121, 220, 57);
        constexpr Color GizmoYHighlight = ColorFromRGB(200, 255, 100);
        constexpr Color GizmoZ = ColorFromRGB(66, 124, 240);
        constexpr Color GizmoZHighlight = ColorFromRGB(140, 200, 255);

        constexpr Color Disabled = Color(0.5f, 0.5f, 0.5f);
    }

    inline Color GetColor(Editor::GizmoAxis axis, const Editor::TransformGizmo& gizmo, Editor::TransformMode mode) {
        if (Input::GetMouselook()) return Colors::Disabled;

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

    inline void DrawGizmoPreview(const Editor::TransformGizmo& gizmo) {
        using namespace Editor;
        if (gizmo.SelectedAxis == GizmoAxis::None) return;
        if (gizmo.State == GizmoState::None) return;

        auto color = [&gizmo] {
            switch (gizmo.SelectedAxis) {
                default:
                case GizmoAxis::X: return Colors::GizmoXHighlight;
                case GizmoAxis::Y: return Colors::GizmoYHighlight;
                case GizmoAxis::Z: return Colors::GizmoZHighlight;
            }
        }();

        if (gizmo.Mode == TransformMode::Translation || gizmo.Mode == TransformMode::Rotation)
            Debug::DrawLine(GizmoPreview::Start, GizmoPreview::End, color);

        if (gizmo.Mode == TransformMode::Scale) {
            auto [up, right] = [&gizmo]() -> Tuple<Vector3, Vector3> {
                switch (gizmo.SelectedAxis) {
                    default:
                    case GizmoAxis::X: return { gizmo.Transform.Up(), gizmo.Transform.Right() };
                    case GizmoAxis::Y: return { gizmo.Transform.Forward(), gizmo.Transform.Right() };
                    case GizmoAxis::Z: return { gizmo.Transform.Up(), gizmo.Transform.Forward() };
                }
            }();

            auto position = gizmo.Transform.Translation();
            auto scale = Editor::GetGizmoScale(position, Camera);
            Debug::DrawPlane(position, right, up, color, scale * 10);
        }
    }

    inline void DrawTranslationGizmo(ID3D12GraphicsCommandList* commandList, const Editor::TransformGizmo& gizmo, const Matrix& viewProjection) {
        using namespace Editor;
        auto sizeScale = Settings::GizmoSize / 5.0f; // arrows have a default size of 5
        auto position = gizmo.Transform.Translation();
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(position, Camera) * sizeScale);
        auto translation = Matrix::CreateTranslation(position);

        auto gizmoDir = Camera.Position - position;
        gizmoDir.Normalize();

        auto DrawAxis = [&](GizmoAxis axis, Vector3 dir) {
            if (std::abs(dir.Dot(gizmoDir)) > TransformGizmo::MaxViewAngle)
                return; // Hide gizmo if camera is aligned to it

            auto rotation = DirectionToRotationMatrix(dir);
            auto transform = rotation * scale * translation * viewProjection;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Translation);
            Debug::DrawArrow(commandList, transform, color);
        };

        if (gizmo.ShowTranslationAxis[0]) DrawAxis(GizmoAxis::X, gizmo.Transform.Forward());
        if (gizmo.ShowTranslationAxis[1]) DrawAxis(GizmoAxis::Y, gizmo.Transform.Up());
        if (gizmo.ShowTranslationAxis[2]) DrawAxis(GizmoAxis::Z, gizmo.Transform.Right());
    }

    inline void DrawRotationGizmo(const Editor::TransformGizmo& gizmo) {
        using namespace Editor;
        auto position = gizmo.Transform.Translation();
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(position, Camera));
        auto translation = Matrix::CreateTranslation(position);

        auto gizmoDir = Camera.Position - position;
        gizmoDir.Normalize();

        auto DrawAxis = [&](GizmoAxis axis, const Vector3& normal, const Vector3& orient) {
            auto cdot = normal.Dot(gizmoDir);
            if (std::abs(cdot) < 1 - TransformGizmo::MaxViewAngle) return; // Don't draw axis at sharp angles

            auto target = ProjectPointOntoPlane(Camera.Position, position, normal);
            //Debug::DrawArrow(position, position + normal * 5, Color(1, 0, 0));
            //Debug::DrawArrow(position, position + orient * 5, Color(1, 1, 0));
            //Debug::DrawArrow(position, target, Color(0, 1, 1));
            auto cameraDir = target - position; // direction towards the camera on this plane
            auto cameraAngle = AngleBetweenVectors(orient, cameraDir, normal); // angle between the camera on this plane and the ref
            // DrawArc() draws on the XY axis, rotate it by 90 on Y to align to XZ axis.
            auto rotation = Matrix::CreateRotationY(DirectX::XM_PIDIV2) * DirectionToRotationMatrix(normal);
            Vector3 arcRef = Vector3::Transform(Vector3::UnitY, rotation); // rotate the arc center ref with it

            cameraAngle += AngleBetweenVectors(arcRef, orient, normal); // center the arc on the orientation vector
            auto transform = rotation * scale * translation;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Rotation);

            if (std::abs(cdot) > TransformGizmo::MaxViewAngle) // Draw solid ring if camera is looking directly at circle
                Debug::DrawRing(Settings::GizmoSize, 0.25f, transform, color);
            else
                Debug::DrawSolidArc(Settings::GizmoSize, 0.25f,
                                    180 * DegToRad, // length
                                    cameraAngle, // offset
                                    transform, color);
        };

        if (gizmo.ShowRotationAxis[0]) DrawAxis(GizmoAxis::X, gizmo.Transform.Forward(), gizmo.Transform.Right());
        if (gizmo.ShowRotationAxis[1]) DrawAxis(GizmoAxis::Y, gizmo.Transform.Up(), gizmo.Transform.Forward());
        if (gizmo.ShowRotationAxis[2]) DrawAxis(GizmoAxis::Z, gizmo.Transform.Right(), gizmo.Transform.Up());
    }

    inline void DrawScaleGizmo(ID3D12GraphicsCommandList* commandList, const Editor::TransformGizmo& gizmo, const Matrix& viewProjection) {
        using namespace Editor;
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(gizmo.Transform.Translation(), Camera));
        auto translation = Matrix::CreateTranslation(gizmo.Transform.Translation());

        auto gizmoDir = Camera.Position - gizmo.Transform.Translation();
        gizmoDir.Normalize();

        auto DrawAxis = [&](GizmoAxis axis) {
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

            auto offset = Matrix::CreateTranslation(rotation.Forward() * Settings::GizmoSize);
            auto transform = rotation * offset * scale * translation * viewProjection;
            auto color = GetColor(axis, gizmo, Editor::TransformMode::Scale);
            Debug::DrawCube(commandList, transform, color);
        };

        if (gizmo.ShowScaleAxis[0]) DrawAxis(GizmoAxis::X);
        if (gizmo.ShowScaleAxis[1]) DrawAxis(GizmoAxis::Y);
        if (gizmo.ShowScaleAxis[2]) DrawAxis(GizmoAxis::Z);
    }
}
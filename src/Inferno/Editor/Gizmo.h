#pragma once

#include "Input.h"
#include "Camera.h"
#include "Editor.Selection.h"
#include "Level.h"

namespace Inferno::Editor {
    enum class GizmoAxis { None, X, Y, Z };
    enum class GizmoState { None, BeginDrag, Dragging, EndDrag, LeftClick, RightClick };
    enum class TransformMode { Translation, Rotation, Scale };

    class TransformGizmo {
        Vector3 CursorStart; // cursor world position at start of drag
        Vector3 Direction; // the axis direction being dragged
        
        Vector3 _prevTranslation; // Scale and Translation
        float _prevAngle = 0; // Rotation
        Matrix StartTransform; // where the gizmo started the drag
        Vector2 _lastMousePosition;
    public:
        GizmoAxis SelectedAxis = GizmoAxis::None;
        GizmoState State;

        TransformMode Mode; // Which handle type was clicked?
        Matrix Transform; // orientation for the gizmo
        Matrix DeltaTransform; // transform since the last update
        float Delta; // Distance or angle changed since last update

        bool Grow; // For scaling. True translates points away from gizmo.
        float TotalDelta; // For UI feedback of total distance or angle traveled

        void Update(Input::SelectionState, const Camera& camera);
        void UpdatePosition();

        Array<bool, 3> ShowTranslationAxis = { true, true, true };
        Array<bool, 3> ShowRotationAxis = { true, true, true };
        Array<bool, 3> ShowScaleAxis = { true, true, true };

        void UpdateAxisVisiblity(SelectionMode);
        void CancelDrag() {
            State = GizmoState::None;
            SelectedAxis = GizmoAxis::None;
        }

        static constexpr auto MaxViewAngle = 0.8f; // Threshold for hiding axis relative to camera position

    private:
        void UpdateDrag(const Camera& camera);
    };

    inline TransformGizmo Gizmo;
    inline Vector3 DebugNearestHit;
    inline float DebugHitDistance;
    inline float DebugAngle;

    Matrix GetTransformFromSelection(Level&, Tag, SelectionMode);
    float GetGizmoScale(const Vector3& position, const Camera& camera);

    namespace GizmoPreview {
        inline Vector3 Start, End;
        inline Vector3 RotationStart;
    }
}
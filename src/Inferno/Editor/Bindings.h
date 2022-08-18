#pragma once
#include <DirectXTK12/Keyboard.h>
#include "Types.h"

namespace Inferno::Editor {
    enum class Binding {
        SideMode,
        PointMode,
        SegmentMode,
        EdgeMode,
        ObjectMode,
        NextItem,
        PreviousItem,
        SegmentForward,
        SegmentBack,
        SelectLinked,
        FocusSelection,
        ZoomExtents,
        AlignViewToFace,
        GizmoTranslation,
        GizmoRotation,
        GizmoScale,
        Delete,
        Insert,
        CameraLeft,
        CameraRight,
        CameraForward,
        CameraBack,
        CameraUp,
        CameraDown,
        CameraRollLeft,
        CameraRollRight,
        ToggleMouselook,
        ClearSelection,
        Copy,
        Paste,
        PasteMirrored,
        Cut,
        Save,
        SaveAs,
        Open,
        Undo,
        Redo,
        ShowHogDialog,
        ShowGotoDialog,
        AlignMarked,
        ResetUVs,
        CycleRenderMode,
        CopyUVsToFaces,
        ConnectSides,
        JoinPoints,
        ToggleMark,
        InsertMirrored,
        JoinTouchingSegments,
        JoinSides,
        DetachSegments,
        DetachSides,
        DetachPoints,
        SplitSegment2,
        MergeSegment,
        ToggleWireframe,
        NewLevel,
        InvertMarked,
        MakeCoplanar
    };

    struct EditorBinding {
        Binding Binding;
        DirectX::Keyboard::Keys Key;
        bool Shift;
        bool Control;
        bool Alt;

        string GetShortcutLabel();
    };

    namespace Bindings {
        string GetShortcut(Binding);

        // Adds a binding that executes when the key is first pressed
        void Add(EditorBinding);

        // Adds a binding that executes every frame the key is held
        void AddRealtime(EditorBinding);
        void Update();

        void LoadDefaults();
    }
}
#pragma once
#include <DirectXTK12/Keyboard.h>
#include "Types.h"
#include "Command.h"
#include "Input.h"

namespace Inferno::Editor {
    enum class EditorAction {
        None = 0,
        SideMode,
        PointMode,
        SegmentMode,
        EdgeMode,
        ObjectMode,
        ToggleWallMode,
        ToggleTextureMode,
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
        HoldMouselook,
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
        ShowHogEditor,
        ShowMissionEditor,
        ShowGotoDialog,
        AlignMarked,
        ResetUVs,
        FitUVs,
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
        MakeCoplanar,
        HideMarks,
        InsertAlignedSegment,
        AveragePoints
    };

    const Command& GetCommandForAction(EditorAction action);
    namespace Commands {
        extern Command NullCommand;
        extern Command ToggleMouselook;
    }

    struct EditorBinding {
        EditorAction Action{};
        Input::Keys Key = Input::Keys::None;

        bool Shift = false;
        bool Control = false;
        bool Alt = false;
        bool Realtime = false;
        const Command* Command = &Commands::NullCommand; // To avoid looking up command every time

        bool operator==(const EditorBinding& rhs) const {
            return Key == rhs.Key && Shift == rhs.Shift && Control == rhs.Control && Alt == rhs.Alt;
        }

        void ClearShortcut() {
            Shift = Control = Alt = false;
            Key = {};
        }

        string GetShortcutLabel() const;
    };

    class EditorBindings {
        List<EditorBinding> _bindings;
    public:
        // Adds a new binding and unbinds any existing actions using the same shortcut
        void Add(EditorBinding binding);
        void Clear() { _bindings.clear(); }
        span<EditorBinding> GetBindings() { return _bindings; }

        EditorBinding* GetBinding(EditorAction action) {
            return Seq::find(_bindings, [&action](auto& b) { return b.Action == action; });
        }

        // Gets the text to display for a shortcut
        string GetShortcut(EditorAction bind) const {
            for (auto& binding : _bindings) {
                if (binding.Action == bind)
                    return binding.GetShortcutLabel();
            }

            return {};
        }

        void Sort() {
            Seq::sortBy(_bindings, [](const EditorBinding& a, const EditorBinding& b) {
                return a.Command->Name < b.Command->Name;
            });
        }

        // Clears a binding that uses the same shortcut as the provided one
        void UnbindExisting(const EditorBinding& binding) {
            if (auto existing = Seq::find(_bindings, [&binding](auto& b) { return b == binding; })) {
                existing->ClearShortcut();
            }
        }

        bool IsBindingHeld(Editor::EditorAction action) {
            auto binding = GetBinding(action);
            return binding ? Input::IsKeyDown(binding->Key) : false;
        }

        Input::Keys GetBindingKey(Editor::EditorAction action) {
            auto binding = GetBinding(action);
            return binding ? binding->Key : Input::Keys::None;
        }
    };

    namespace Bindings {
        inline EditorBindings Active, Default;

        void Update();
        void LoadDefaults();
        bool IsReservedKey(Input::Keys);
    }
}
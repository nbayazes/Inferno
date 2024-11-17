#include "pch.h"
#include "Bindings.h"
#include "Editor.Clipboard.h"
#include "Editor.Geometry.h"
#include "Editor.h"
#include "Input.h"
#include "imgui_local.h"
#include "Editor.Texture.h"
#include "Editor.Segment.h"
#include "Editor.IO.h"
#include "Game.h"

using namespace DirectX;

using Keys = Inferno::Input::Keys;

namespace Inferno::Editor {
    namespace Commands {
        Command NullCommand{
            .Action = [] {},
            .Name = "Null Command"
        };

        Command SelectionNext{
            .Action = [] { Selection.NextItem(); },
            .Name = "Select Next"
        };
        Command SelectionPrevious{
            .Action = [] { Selection.PreviousItem(); },
            .Name = "Select Previous"
        };

        Command SelectionForward{
            .Action = [] { Selection.Forward(); },
            .Name = "Select Forward"
        };

        Command SelectionBack{
            .Action = [] { Selection.Back(); },
            .Name = "Select Backwards"
        };

        Command SelectLinked{
            .Action = [] { Selection.SelectLinked(); },
            .Name = "Select Linked"
        };

        Command SetFaceMode{
            .Action = [] { Editor::SetMode(SelectionMode::Face); },
            .Name = "Mode: Face"
        };

        Command SetPointMode{
            .Action = [] { Editor::SetMode(SelectionMode::Point); },
            .Name = "Mode: Point"
        };

        Command SetEdgeMode{
            .Action = [] { Editor::SetMode(SelectionMode::Edge); },
            .Name = "Mode: Edge"
        };

        Command SetSegmentMode{
            .Action = [] { Editor::SetMode(SelectionMode::Segment); },
            .Name = "Mode: Segment"
        };

        Command SetObjectMode{
            .Action = [] { Editor::SetMode(SelectionMode::Object); },
            .Name = "Mode: Object"
        };

        Command ToggleWallMode{
            .Action = [] { Editor::ToggleWallMode(); },
            .Name = "Toggle Wall Mode"
        };

        Command ToggleTextureMode{
            .Action = [] { Editor::ToggleTextureMode(); },
            .Name = "Toggle Texture Mode"
        };

        Command CameraForward{
            .Action = [] { Editor::EditorCamera.MoveForward(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Forward"
        };

        Command CameraBack{
            .Action = [] { Editor::EditorCamera.MoveBack(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Back"
        };

        Command CameraLeft{
            .Action = [] { Editor::EditorCamera.MoveLeft(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Left"
        };

        Command CameraRight{
            .Action = [] { Editor::EditorCamera.MoveRight(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Right"
        };

        Command CameraUp{
            .Action = [] { Editor::EditorCamera.MoveUp(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Up"
        };

        Command CameraDown{
            .Action = [] { Editor::EditorCamera.MoveDown(Clock.GetFrameTimeSeconds() * Settings::Editor.MoveSpeed); },
            .Name = "Camera: Down"
        };

        Command CameraRollLeft{
            .Action = [] { Editor::EditorCamera.Roll(Clock.GetFrameTimeSeconds() * 2); },
            .Name = "Camera: Roll Left"
        };

        Command CameraRollRight{
            .Action = [] { Editor::EditorCamera.Roll(-Clock.GetFrameTimeSeconds() * 2); },
            .Name = "Camera: Roll Right"
        };

        Command ToggleMouselook{
            .Action = [] { Input::SetMouseMode(Input::GetMouseMode() == Input::MouseMode::Mouselook ? Input::MouseMode::Normal : Input::MouseMode::Mouselook); },
            .Name = "Toggle Mouselook"
        };

        Command OpenHogEditor{
            .Action = [] { Events::ShowDialog(DialogType::HogEditor); },
            .CanExecute = [] { return Game::Mission.has_value(); },
            .Name = "Hog Editor"
        };

        Command OpenMissionEditor{
            .Action = [] { Events::ShowDialog(DialogType::MissionEditor); },
            .CanExecute = [] { return Game::Mission.has_value(); },
            .Name = "Mission Editor"
        };

        Command GotoSegment{
            .Action = [] { Events::ShowDialog(DialogType::GotoSegment); },
            .Name = "Go to Segment"
        };

        Command GotoObject{
            .Action = [] { Events::ShowDialog(DialogType::GotoObject); },
            .Name = "Go to Object"
        };

        Command GotoWall{
            .Action = [] { Events::ShowDialog(DialogType::GotoWall); },
            .Name = "Go to Wall"
        };

        Command HideMarks{ .Action = [] {}, .Name = "Hide Marks" };
        Command HoldMouselook{ .Action = [] {}, .Name = "Hold Mouselook" };
    }

    const Command& GetCommandForAction(EditorAction action) {
        switch (action) {
            case EditorAction::NextItem: return Commands::SelectionNext;
            case EditorAction::PreviousItem: return Commands::SelectionPrevious;
            case EditorAction::SegmentForward: return Commands::SelectionForward;
            case EditorAction::SegmentBack: return Commands::SelectionBack;
            case EditorAction::SelectLinked: return Commands::SelectLinked;
            case EditorAction::SideMode: return Commands::SetFaceMode;
            case EditorAction::PointMode: return Commands::SetPointMode;
            case EditorAction::EdgeMode: return Commands::SetEdgeMode;
            case EditorAction::SegmentMode: return Commands::SetSegmentMode;
            case EditorAction::ObjectMode: return Commands::SetObjectMode;
            case EditorAction::ToggleWallMode: return Commands::ToggleWallMode;
            case EditorAction::ToggleTextureMode: return Commands::ToggleTextureMode;
            case EditorAction::CameraForward: return Commands::CameraForward;
            case EditorAction::CameraBack: return Commands::CameraBack;
            case EditorAction::CameraLeft: return Commands::CameraLeft;
            case EditorAction::CameraRight: return Commands::CameraRight;
            case EditorAction::CameraUp: return Commands::CameraUp;
            case EditorAction::CameraDown: return Commands::CameraDown;
            case EditorAction::CameraRollLeft: return Commands::CameraRollLeft;
            case EditorAction::CameraRollRight: return Commands::CameraRollRight;
            case EditorAction::ToggleMouselook: return Commands::ToggleMouselook;
            case EditorAction::ClearSelection: return Commands::ClearMarked;
            case EditorAction::Delete: return Commands::Delete;
            case EditorAction::Insert: return Commands::Insert;
            case EditorAction::Copy: return Commands::Copy;
            case EditorAction::Cut: return Commands::Cut;
            case EditorAction::Paste: return Commands::Paste;
            case EditorAction::PasteMirrored: return Commands::PasteMirrored;
            case EditorAction::Save: return Commands::Save;
            case EditorAction::SaveAs: return Commands::SaveAs;
            case EditorAction::Open: return Commands::Open;
            case EditorAction::Undo: return Commands::Undo;
            case EditorAction::Redo: return Commands::Redo;
            case EditorAction::AlignViewToFace: return Commands::AlignViewToFace;
            case EditorAction::FocusSelection: return Commands::FocusSelection;
            case EditorAction::ZoomExtents: return Commands::ZoomExtents;
            case EditorAction::ShowHogEditor: return Commands::OpenHogEditor;
            case EditorAction::ShowMissionEditor: return Commands::OpenMissionEditor;
            case EditorAction::ShowGotoSegment: return Commands::GotoSegment;
            case EditorAction::ShowGotoObject: return Commands::GotoObject;
            case EditorAction::ShowGotoWall: return Commands::GotoWall;
            case EditorAction::AlignMarked: return Commands::AlignMarked;
            case EditorAction::ResetUVs: return Commands::ResetUVs;
            case EditorAction::FitUVs: return Commands::FitUVs;
            case EditorAction::CycleRenderMode: return Commands::CycleRenderMode;
            case EditorAction::ToggleWireframe: return Commands::ToggleWireframe;

            case EditorAction::CopyUVsToFaces: return Commands::CopyUVsToFaces;
            case EditorAction::ConnectSides: return Commands::ConnectSides;
            case EditorAction::JoinPoints: return Commands::JoinPoints;
            case EditorAction::ToggleMark: return Commands::ToggleMarked;
            case EditorAction::InsertMirrored: return Commands::InsertMirrored;
            case EditorAction::JoinTouchingSegments: return Commands::JoinTouchingSegments;
            case EditorAction::JoinSides: return Commands::JoinSides;
            case EditorAction::DetachSegments: return Commands::DetachSegments;
            case EditorAction::DetachSides: return Commands::DetachSides;
            case EditorAction::DetachPoints: return Commands::DetachPoints;
            case EditorAction::SplitSegment2: return Commands::SplitSegment2;
            case EditorAction::MergeSegment: return Commands::MergeSegment;
            case EditorAction::NewLevel: return Commands::NewLevel;
            case EditorAction::InvertMarked: return Commands::InvertMarked;
            case EditorAction::MakeCoplanar: return Commands::MakeCoplanar;
            case EditorAction::HideMarks: return Commands::HideMarks;
            case EditorAction::HoldMouselook: return Commands::HoldMouselook;
            case EditorAction::InsertAlignedSegment: return Commands::InsertAlignedSegment;
            case EditorAction::AveragePoints: return Commands::AveragePoints;
        }

        return Commands::NullCommand;
    }

    string EditorBinding::GetShortcutLabel() const {
        if (!Shift && !Control && !Alt) return Input::KeyToString(Key);
        string modifiers;
        if (Control) modifiers = "Ctrl";
        if (Shift) modifiers = modifiers.empty() ? "Shift" : modifiers + "+Shift";
        if (Alt) modifiers = modifiers.empty() ? "Alt" : modifiers + "+Alt";
        return modifiers + "+" + Input::KeyToString(Key);
    }

    void EditorBindings::Add(EditorBinding binding) {
        if (binding.Action == EditorAction::None) return;

        switch (binding.Action) {
            case EditorAction::CameraBack:
            case EditorAction::CameraForward:
            case EditorAction::CameraUp:
            case EditorAction::CameraDown:
            case EditorAction::CameraLeft:
            case EditorAction::CameraRight:
            case EditorAction::CameraRollLeft:
            case EditorAction::CameraRollRight:
                binding.Realtime = true;
                break;
        }

        UnbindExisting(binding);

        if (!binding.Command || binding.Command == &Commands::NullCommand)
            binding.Command = &GetCommandForAction(binding.Action);
        _bindings.push_back(binding);
    }
}

namespace Inferno::Editor::Bindings {
    void Update() {
        auto& io = ImGui::GetIO();
        auto imguiWantsFocus = io.WantCaptureMouse;

        for (auto& binding : Bindings::Active.GetBindings()) {
            if (binding.Realtime) {
                // Realtime bindings are executed constantly
                if (Input::IsKeyDown(binding.Key) &&
                    binding.Shift == Input::ShiftDown &&
                    binding.Alt == Input::AltDown &&
                    binding.Control == Input::ControlDown) {
                    if (binding.Command)
                        binding.Command->Execute();
                }
            }
            else {
                // don't execute navigation key bindings when imgui has focus
                if ((binding.Key == Keys::Tab && imguiWantsFocus) ||
                    (binding.Key == Keys::Left && imguiWantsFocus) ||
                    (binding.Key == Keys::Right && imguiWantsFocus) ||
                    (binding.Key == Keys::Up && imguiWantsFocus) ||
                    (binding.Key == Keys::Down && imguiWantsFocus) ||
                    (binding.Key == Keys::Space && imguiWantsFocus))
                    continue;

                // Special case shift for mode bindings
                auto shiftOverride =
                    binding.Action == EditorAction::PointMode ||
                    binding.Action == EditorAction::EdgeMode ||
                    binding.Action == EditorAction::SideMode ||
                    binding.Action == EditorAction::SegmentMode;

                if (Input::IsKeyPressed(binding.Key) &&
                    (binding.Shift == Input::ShiftDown || shiftOverride) &&
                    binding.Alt == Input::AltDown &&
                    binding.Control == Input::ControlDown) {
                    if (binding.Command)
                        binding.Command->Execute();
                }
            }
        }
    }

    void LoadDefaults() {
        auto& bindings = Default;
        bindings.Add({ .Action = EditorAction::PointMode, .Key = Keys::D1 });
        bindings.Add({ .Action = EditorAction::EdgeMode, .Key = Keys::D2 });
        bindings.Add({ .Action = EditorAction::SideMode, .Key = Keys::D3 });
        bindings.Add({ .Action = EditorAction::SegmentMode, .Key = Keys::D4 });
        bindings.Add({ .Action = EditorAction::ObjectMode, .Key = Keys::D5 });
        bindings.Add({ .Action = EditorAction::ToggleWallMode, .Key = Keys::D6 });
        bindings.Add({ .Action = EditorAction::ToggleTextureMode, .Key = Keys::D7 });
        bindings.Add({ .Action = EditorAction::NextItem, .Key = Keys::Right });
        bindings.Add({ .Action = EditorAction::PreviousItem, .Key = Keys::Left });
        bindings.Add({ .Action = EditorAction::SelectLinked, .Key = Keys::Tab });
        bindings.Add({ .Action = EditorAction::SegmentForward, .Key = Keys::Up });
        bindings.Add({ .Action = EditorAction::SelectLinked, .Key = Keys::Up, .Shift = true });
        bindings.Add({ .Action = EditorAction::SegmentBack, .Key = Keys::Down });
        bindings.Add({ .Action = EditorAction::Delete, .Key = Keys::Delete });
        bindings.Add({ .Action = EditorAction::Delete, .Key = Keys::Back });
        bindings.Add({ .Action = EditorAction::Insert, .Key = Keys::Insert });
        bindings.Add({ .Action = EditorAction::ClearSelection, .Key = Keys::Escape });

        bindings.Add({ .Action = EditorAction::FocusSelection, .Key = Keys::F });
        bindings.Add({ .Action = EditorAction::AlignViewToFace, .Key = Keys::F, .Shift = true });

        bindings.Add({ .Action = EditorAction::CameraForward, .Key = Keys::W });
        bindings.Add({ .Action = EditorAction::CameraBack, .Key = Keys::S });
        bindings.Add({ .Action = EditorAction::CameraLeft, .Key = Keys::A });
        bindings.Add({ .Action = EditorAction::CameraRight, .Key = Keys::D });
        bindings.Add({ .Action = EditorAction::CameraUp, .Key = Keys::E });
        bindings.Add({ .Action = EditorAction::CameraDown, .Key = Keys::Q });
        bindings.Add({ .Action = EditorAction::CameraRollLeft, .Key = Keys::Q, .Shift = true });
        bindings.Add({ .Action = EditorAction::CameraRollRight, .Key = Keys::E, .Shift = true });

        bindings.Add({ .Action = EditorAction::ToggleMouselook, .Key = Keys::Z });

        bindings.Add({ .Action = EditorAction::Copy, .Key = Keys::C, .Control = true });
        bindings.Add({ .Action = EditorAction::Cut, .Key = Keys::X, .Control = true });
        bindings.Add({ .Action = EditorAction::Paste, .Key = Keys::V, .Control = true });
        bindings.Add({ .Action = EditorAction::PasteMirrored, .Key = Keys::V, .Shift = true, .Control = true });

        bindings.Add({ .Action = EditorAction::Save, .Key = Keys::S, .Control = true });
        bindings.Add({ .Action = EditorAction::SaveAs, .Key = Keys::S, .Shift = true, .Control = true });
        bindings.Add({ .Action = EditorAction::Open, .Key = Keys::O, .Control = true });

        bindings.Add({ .Action = EditorAction::Undo, .Key = Keys::Z, .Control = true });
        bindings.Add({ .Action = EditorAction::Redo, .Key = Keys::Z, .Shift = true, .Control = true });
        bindings.Add({ .Action = EditorAction::Redo, .Key = Keys::Y, .Control = true });

        bindings.Add({ .Action = EditorAction::AlignMarked, .Key = Keys::T });
        bindings.Add({ .Action = EditorAction::AlignMarked, .Key = Keys::A, .Control = true });
        bindings.Add({ .Action = EditorAction::ResetUVs, .Key = Keys::R });
        bindings.Add({ .Action = EditorAction::ResetUVs, .Key = Keys::R, .Control = true });
        bindings.Add({ .Action = EditorAction::CopyUVsToFaces, .Key = Keys::O });
        bindings.Add({ .Action = EditorAction::ToggleMark, .Key = Keys::Space });

        bindings.Add({ .Action = EditorAction::CycleRenderMode, .Key = Keys::F4 });
        //bindings.Add({ .Action = EditorAction::ToggleWireframe, .Key = Keys::F3 });
        bindings.Add({ .Action = EditorAction::InsertMirrored, .Key = Keys::Insert, .Shift = true });

        bindings.Add({ .Action = EditorAction::ConnectSides, .Key = Keys::C });
        bindings.Add({ .Action = EditorAction::JoinSides, .Key = Keys::C, .Shift = true });

        bindings.Add({ .Action = EditorAction::JoinTouchingSegments, .Key = Keys::J });
        bindings.Add({ .Action = EditorAction::JoinPoints, .Key = Keys::J, .Shift = true });

        bindings.Add({ .Action = EditorAction::DetachSegments, .Key = Keys::D, .Control = true });
        bindings.Add({ .Action = EditorAction::DetachSides, .Key = Keys::D, .Shift = true });
        bindings.Add({ .Action = EditorAction::DetachPoints, .Key = Keys::D, .Shift = true, .Control = true });

        bindings.Add({ .Action = EditorAction::SplitSegment2, .Key = Keys::S, .Shift = true });
        bindings.Add({ .Action = EditorAction::MergeSegment, .Key = Keys::M });
        bindings.Add({ .Action = EditorAction::NewLevel, .Key = Keys::N, .Control = true });
        bindings.Add({ .Action = EditorAction::InvertMarked, .Key = Keys::I, .Control = true });
        bindings.Add({ .Action = EditorAction::MakeCoplanar, .Key = Keys::P });

        bindings.Add({ .Action = EditorAction::ShowHogEditor, .Key = Keys::H, .Control = true });
        bindings.Add({ .Action = EditorAction::ShowMissionEditor, .Key = Keys::M, .Control = true });
        bindings.Add({ .Action = EditorAction::ShowGotoSegment, .Key = Keys::G, .Control = true });
        bindings.Add({ .Action = EditorAction::ShowGotoObject, .Key = Keys::G, .Shift = true, .Control = true });
        bindings.Add({ .Action = EditorAction::ShowGotoWall, .Key = Keys::G, .Shift = true, .Control = true, .Alt = true });
        bindings.Add({ .Action = EditorAction::HoldMouselook });
        bindings.Add({ .Action = EditorAction::HideMarks, .Key = Keys::OemTilde });
        bindings.Add({ .Action = EditorAction::InsertAlignedSegment, .Key = Keys::Insert, .Control = true });
        bindings.Add({ .Action = EditorAction::AveragePoints, .Key = Keys::V });

        Active = Default;
    }

    bool IsReservedKey(Keys key) {
        switch (key) {
            case Keys::LeftWindows:
            case Keys::RightWindows:
            case Keys::Pause:
            case Keys::Scroll:
            case Keys::PrintScreen:
            case Keys::LeftAlt:
            case Keys::RightAlt:
            case Keys::LeftShift:
            case Keys::RightShift:
            case Keys::LeftControl:
            case Keys::RightControl:
            case Keys::NumLock:
            case Keys::F1:
            case Keys::F2:
            case Keys::F5:
            case Keys::F6:
            case Keys::F7:
            case Keys::F8:
                return true;
            default:
                return false;
        }
    }
}

#include "pch.h"
#include "Bindings.h"
#include "Editor.h"
#include "Input.h"
#include "Graphics/Render.h"
#include "imgui_local.h"
#include "WindowsDialogs.h"
#include "Editor.Texture.h"
#include "Editor.Segment.h"
#include "Editor.IO.h"

using namespace DirectX;

using Keys = DirectX::Keyboard::Keys;

namespace Inferno::Editor {

    constexpr string KeyToString(Keys key) {
        switch (key) {
            case Keys::Back: return "Back";
            case Keys::Tab: return "Tab";
            case Keys::Enter: return "Enter";
            case Keys::Escape: return "Esc";
            case Keys::Space: return "Space";
            case Keys::PageUp: return "PgUp";
            case Keys::PageDown: return "PgDn";
            case Keys::End: return "End";
            case Keys::Home: return "Home";
            case Keys::Left: return "Left";
            case Keys::Up: return "Up";
            case Keys::Right: return "Right";
            case Keys::Down: return "Down";
            case Keys::Insert: return "Ins";
            case Keys::Delete: return "Del";
            case Keys::A: return "A";
            case Keys::B: return "B";
            case Keys::C: return "C";
            case Keys::D: return "D";
            case Keys::E: return "E";
            case Keys::F: return "F";
            case Keys::G: return "G";
            case Keys::H: return "H";
            case Keys::I: return "I";
            case Keys::J: return "J";
            case Keys::K: return "K";
            case Keys::L: return "L";
            case Keys::M: return "M";
            case Keys::N: return "N";
            case Keys::O: return "O";
            case Keys::P: return "P";
            case Keys::Q: return "Q";
            case Keys::R: return "R";
            case Keys::S: return "S";
            case Keys::T: return "T";
            case Keys::U: return "U";
            case Keys::V: return "V";
            case Keys::W: return "W";
            case Keys::X: return "X";
            case Keys::Y: return "Y";
            case Keys::Z: return "Z";

            case Keys::F1: return "F1";
            case Keys::F2: return "F2";
            case Keys::F3: return "F3";
            case Keys::F4: return "F4";
            case Keys::F5: return "F5";
            case Keys::F6: return "F6";
            case Keys::F7: return "F7";
            case Keys::F8: return "F8";
            case Keys::F9: return "F9";
            case Keys::F10: return "F10";
            case Keys::F11: return "F11";
            case Keys::F12: return "F12";

            default:
                return string(1, key);
        }
    }

    string EditorBinding::GetShortcutLabel() {
        if (!Shift && !Control && !Alt) return KeyToString(Key);
        string modifiers;
        if (Control) modifiers = "Ctrl";
        if (Shift) modifiers = modifiers.empty() ? "Shift" : modifiers + "+Shift";
        if (Alt) modifiers = modifiers.empty() ? "Alt" : modifiers + "+Alt";
        return modifiers + "+" + KeyToString(Key);
    }

    //string GetActionName(Binding action) {
    //    switch (action) {
    //        case Binding::NextItem: return "Next";
    //        case Binding::PreviousItem: return "Previous";
    //        case Binding::SegmentForward: return "Forward";
    //        case Binding::SegmentBack: return "Backward";
    //        case Binding::SelectLinked: return "Select Other Side";
    //        case Binding::SideMode: return "Side Mode";
    //        case Binding::PointMode: return "Point Mode";
    //        case Binding::EdgeMode: return "Edge Mode";
    //        case Binding::SegmentMode:return "Segment Mode";
    //        case Binding::ObjectMode: return "Object Mode";
    //        //case Binding::CameraForward: return "Edge Mode";
    //        //case Binding::CameraBack: return "Edge Mode";
    //        //case Binding::CameraLeft: return "Edge Mode";
    //        //case Binding::CameraRight: return "Edge Mode";
    //        //case Binding::CameraUp: return "Edge Mode";
    //        //case Binding::CameraDown: return "Edge Mode";
    //        //case Binding::CameraRollLeft: return "Edge Mode";
    //        //case Binding::CameraRollRight: return "Edge Mode";
    //        case Binding::ToggleMouselook: return "Mouse Look";
    //        case Binding::ClearSelection: return "Clear Selection";
    //        case Binding::Delete: return "Delete";
    //        case Binding::Insert: return "Insert";
    //        case Binding::Copy: return "Copy";
    //        case Binding::Cut: return "Cut";
    //        case Binding::Paste: return "Paste";
    //        case Binding::PasteMirrored: return "Paste Mirrored";
    //        case Binding::Save: return "Save";
    //        case Binding::SaveAs: return "Save As";
    //        case Binding::Open: return "Open";
    //        case Binding::Undo: return "Undo";
    //        case Binding::Redo: return "Redo";
    //        case Binding::AlignViewToFace: return "Align View To Face";
    //        case Binding::FocusSelection: return "Focus Selection";
    //        case Binding::ZoomExtents: return "Zoom Extents";
    //        case Binding::ShowHogDialog: return "Show HOG Editor";
    //        case Binding::ShowGotoDialog: return "Go To Segment";
    //        case Binding::AlignMarked: return "Edge";
    //        case Binding::ResetUVs: return "Edge";
    //        case Binding::CycleRenderMode: return "Edge";
    //        case Binding::CopyUVsToOtherSide: return "Edge";
    //        case Binding::ConnectSegments: return "Edge";
    //        case Binding::ConnectPoints: return "Edge";
    //        case Binding::ToggleSelection: return "Edge";
    //        case Binding::MirrorSegment: return "Edge";
    //        case Binding::JoinTouchingSegments: return "Edge";
    //        default: return "Unknown";
    //    }
    //}
}

namespace Inferno::Editor::Bindings {

    namespace {
        List<EditorBinding> _bindings, _realtimeBindings;
    }

    //Dictionary<Action, std::function<void(void)>> CommandTable = {
    //    { Binding::NextItem, [] { Selection.NextItem(); } },
    //    { Binding::PreviousItem, [] { Selection.PreviousItem(); } },
    //    { Binding::SegmentForward, [] { Selection.Forward(); } },
    //    { Binding::SegmentBack, [] { Selection.Back(); } },
    //    { Binding::SelectLinked, [] { Selection.SelectLinked(); } },
    //    { Binding::SideMode, [] { Editor::SetMode(SelectionMode::Face); } },
    //    { Binding::PointMode, [] { Editor::SetMode(SelectionMode::Point); } },
    //    { Binding::EdgeMode, [] { Editor::SetMode(SelectionMode::Edge); } },
    //    { Binding::SegmentMode, [] { Editor::SetMode(SelectionMode::Segment); } },
    //    { Binding::ObjectMode, [] { Editor::SetMode(SelectionMode::Object); } },
    //    { Binding::CameraForward, [] { Render::Camera.MoveForward(Render::FrameTime); } },
    //    { Binding::CameraBack, [] { Render::Camera.MoveBack(Render::FrameTime); } },
    //    { Binding::CameraLeft, [] { Render::Camera.MoveLeft(Render::FrameTime); } },
    //    { Binding::CameraRight, [] { Render::Camera.MoveRight(Render::FrameTime); } },
    //    { Binding::CameraUp, [] { Render::Camera.MoveUp(Render::FrameTime); } },
    //    { Binding::CameraDown, [] { Render::Camera.MoveDown(Render::FrameTime); } },
    //    { Binding::CameraDown, [] { Render::Camera.MoveDown(Render::FrameTime); } },
    //    { Binding::CameraDown, [] { Render::Camera.MoveDown(Render::FrameTime); } },
    //    { Binding::CameraDown, [] { Render::Camera.MoveDown(Render::FrameTime); } },
    //    { Binding::CameraDown, [] { Render::Camera.MoveDown(Render::FrameTime); } },
    //};

    void ExecuteAction(Binding action) {
        try {
            switch (action) {
                case Binding::NextItem: Selection.NextItem(); break;
                case Binding::PreviousItem: Selection.PreviousItem(); break;
                case Binding::SegmentForward: Selection.Forward(); break;
                case Binding::SegmentBack: Selection.Back(); break;
                case Binding::SelectLinked: Selection.SelectLinked(); break;
                case Binding::SideMode: Editor::SetMode(SelectionMode::Face); break;
                case Binding::PointMode: Editor::SetMode(SelectionMode::Point); break;
                case Binding::EdgeMode: Editor::SetMode(SelectionMode::Edge); break;
                case Binding::SegmentMode: Editor::SetMode(SelectionMode::Segment); break;
                case Binding::ObjectMode: Editor::SetMode(SelectionMode::Object); break;
                case Binding::CameraForward: Render::Camera.MoveForward(Render::FrameTime); break;
                case Binding::CameraBack: Render::Camera.MoveBack(Render::FrameTime); break;
                case Binding::CameraLeft: Render::Camera.MoveLeft(Render::FrameTime); break;
                case Binding::CameraRight: Render::Camera.MoveRight(Render::FrameTime); break;
                case Binding::CameraUp: Render::Camera.MoveUp(Render::FrameTime); break;
                case Binding::CameraDown: Render::Camera.MoveDown(Render::FrameTime); break;
                case Binding::CameraRollLeft: Render::Camera.Roll(Render::FrameTime); break;
                case Binding::CameraRollRight: Render::Camera.Roll(-Render::FrameTime); break;
                case Binding::ToggleMouselook: Input::SetMouselook(!Input::GetMouselook()); break;
                case Binding::ClearSelection: Commands::ClearMarked(); break;
                case Binding::Delete: Commands::Delete(); break;
                case Binding::Insert: Commands::Insert(); break;
                case Binding::Copy: Commands::Copy(); break;
                case Binding::Cut: Commands::Cut(); break;
                case Binding::Paste: Commands::Paste(); break;
                case Binding::PasteMirrored: Commands::PasteMirrored(); break;
                case Binding::Save: Commands::Save(); break;
                case Binding::SaveAs: Commands::SaveAs(); break;
                case Binding::Open: Commands::Open(); break;
                case Binding::Undo: Commands::Undo(); break;
                case Binding::Redo: Commands::Redo(); break;
                case Binding::AlignViewToFace: Commands::AlignViewToFace(); break;
                case Binding::FocusSelection: Commands::FocusSegment(); break;
                case Binding::ZoomExtents: Commands::ZoomExtents(); break;
                case Binding::ShowHogDialog: Events::ShowDialog(DialogType::HogEditor); break;
                case Binding::ShowGotoDialog: Events::ShowDialog(DialogType::GotoSegment); break;
                case Binding::AlignMarked: Commands::AlignMarked(); break;
                case Binding::ResetUVs: Commands::ResetUVs(); break;
                case Binding::CycleRenderMode: Commands::CycleRenderMode(); break;
                case Binding::ToggleWireframe: Commands::ToggleWireframe(); break;
                    
                case Binding::CopyUVsToFaces: Commands::CopyUVsToFaces(); break;
                case Binding::ConnectSides: Commands::ConnectSides(); break;
                case Binding::JoinPoints: Commands::JoinPoints(); break;
                case Binding::ToggleMark: Commands::ToggleMarked(); break;
                case Binding::InsertMirrored: Commands::InsertMirrored(); break;
                case Binding::JoinTouchingSegments: Commands::JoinTouchingSegments(); break;
                case Binding::JoinSides: Commands::JoinSides(); break;
                case Binding::DetachSegments: Commands::DetachSegments(); break;
                case Binding::DetachSides: Commands::DetachSides(); break;
                case Binding::DetachPoints: Commands::DetachPoints(); break;
                case Binding::SplitSegment2: Commands::SplitSegment2(); break;
                case Binding::NewLevel: Commands::NewLevel(); break;
                case Binding::InvertMarked: Commands::InvertMarked(); break;
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void Add(EditorBinding binding) {
        _bindings.push_back(binding);
    }

    void AddRealtime(EditorBinding binding) {
        _realtimeBindings.push_back(binding);
    }

    void Update() {
        auto& io = ImGui::GetIO();
        auto imguiWantsFocus = io.WantTextInput || io.WantCaptureKeyboard;

        for (auto& binding : _bindings) {
            // don't execute navigation key bindings when imgui has focus
            if ((binding.Key == Keys::Tab && imguiWantsFocus) ||
                (binding.Key == Keys::Left && imguiWantsFocus) ||
                (binding.Key == Keys::Right && imguiWantsFocus) ||
                (binding.Key == Keys::Up && imguiWantsFocus) ||
                (binding.Key == Keys::Down && imguiWantsFocus))
                continue;

            if (Input::Keyboard.IsKeyPressed(binding.Key) &&
                binding.Shift == Input::ShiftDown &&
                binding.Alt == Input::AltDown &&
                binding.Control == Input::ControlDown) {
                ExecuteAction(binding.Binding);
            }
        }

        for (auto& binding : _realtimeBindings) {
            if (Input::IsKeyDown(binding.Key) &&
                binding.Shift == Input::ShiftDown &&
                binding.Alt == Input::AltDown &&
                binding.Control == Input::ControlDown) {
                ExecuteAction(binding.Binding);
            }
        }
    }

    void LoadDefaults() {
        Bindings::Add({ Binding::PointMode, Keys::D1 });
        Bindings::Add({ Binding::PointMode, Keys::D1, true });
        Bindings::Add({ Binding::EdgeMode, Keys::D2 });
        Bindings::Add({ Binding::EdgeMode, Keys::D2, true });
        Bindings::Add({ Binding::SideMode, Keys::D3 });
        Bindings::Add({ Binding::SideMode, Keys::D3, true });
        Bindings::Add({ Binding::SegmentMode, Keys::D4 });
        Bindings::Add({ Binding::SegmentMode, Keys::D4, true });
        Bindings::Add({ Binding::ObjectMode, Keys::D5 });
        Bindings::Add({ Binding::NextItem, Keys::Right });
        Bindings::Add({ Binding::PreviousItem, Keys::Left });
        Bindings::Add({ Binding::SelectLinked, Keys::Tab });
        Bindings::Add({ Binding::SegmentForward, Keys::Up });
        Bindings::Add({ Binding::SelectLinked, Keys::Up, true });
        Bindings::Add({ Binding::SegmentBack, Keys::Down });
        Bindings::Add({ Binding::Delete, Keys::Delete });
        Bindings::Add({ Binding::Delete, Keys::Back });
        Bindings::Add({ Binding::Insert, Keys::Insert });
        Bindings::Add({ Binding::ClearSelection, Keys::Escape });

        Bindings::Add({ .Binding = Binding::FocusSelection, .Key = Keys::F });
        Bindings::Add({ .Binding = Binding::AlignViewToFace, .Key = Keys::F, .Shift = true });

        Bindings::AddRealtime({ Binding::CameraForward, Keys::W });
        Bindings::AddRealtime({ Binding::CameraBack, Keys::S });
        Bindings::AddRealtime({ Binding::CameraLeft, Keys::A });
        Bindings::AddRealtime({ Binding::CameraRight, Keys::D });
        Bindings::AddRealtime({ Binding::CameraUp, Keys::E });
        Bindings::AddRealtime({ Binding::CameraDown, Keys::Q });
        Bindings::AddRealtime({ .Binding = Binding::CameraRollLeft, .Key = Keys::Q, .Shift = true });
        Bindings::AddRealtime({ .Binding = Binding::CameraRollRight, .Key = Keys::E, .Shift = true });

        Bindings::Add({ Binding::ToggleMouselook, Keys::Z });

        Bindings::Add({ .Binding = Binding::Copy, .Key = Keys::C, .Control = true });
        Bindings::Add({ .Binding = Binding::Cut, .Key = Keys::X, .Control = true });
        Bindings::Add({ .Binding = Binding::Paste, .Key = Keys::V, .Control = true });
        Bindings::Add({ .Binding = Binding::PasteMirrored, .Key = Keys::V, .Shift = true, .Control = true });

        Bindings::Add({ .Binding = Binding::Save, .Key = Keys::S, .Control = true });
        Bindings::Add({ .Binding = Binding::SaveAs, .Key = Keys::S, .Shift = true, .Control = true });
        Bindings::Add({ .Binding = Binding::Open, .Key = Keys::O, .Control = true });

        Bindings::Add({ .Binding = Binding::Undo, .Key = Keys::Z, .Control = true });
        Bindings::Add({ .Binding = Binding::Redo, .Key = Keys::Z, .Shift = true, .Control = true });
        Bindings::Add({ .Binding = Binding::Redo, .Key = Keys::Y, .Control = true });

        Bindings::Add({ .Binding = Binding::ShowHogDialog, .Key = Keys::H, .Control = true });
        Bindings::Add({ .Binding = Binding::ShowGotoDialog, .Key = Keys::G, .Control = true });

        Bindings::Add({ .Binding = Binding::AlignMarked, .Key = Keys::T });
        Bindings::Add({ .Binding = Binding::AlignMarked, .Key = Keys::A, .Control = true });
        Bindings::Add({ .Binding = Binding::ResetUVs, .Key = Keys::R });
        Bindings::Add({ .Binding = Binding::ResetUVs, .Key = Keys::R, .Control = true });
        Bindings::Add({ .Binding = Binding::CopyUVsToFaces, .Key = Keys::O });
        Bindings::Add({ .Binding = Binding::ToggleMark, .Key = Keys::Space });

        Bindings::Add({ .Binding = Binding::CycleRenderMode, .Key = Keys::F4 });
        Bindings::Add({ .Binding = Binding::ToggleWireframe, .Key = Keys::F3 });
        Bindings::Add({ .Binding = Binding::InsertMirrored, .Key = Keys::Insert, .Shift = true });

        Bindings::Add({ .Binding = Binding::ConnectSides, .Key = Keys::C });
        Bindings::Add({ .Binding = Binding::JoinSides, .Key = Keys::C, .Shift = true });

        Bindings::Add({ .Binding = Binding::JoinTouchingSegments, .Key = Keys::J });
        Bindings::Add({ .Binding = Binding::JoinPoints, .Key = Keys::J, .Shift = true });

        Bindings::Add({ .Binding = Binding::DetachSegments, .Key = Keys::D, .Control = true });
        Bindings::Add({ .Binding = Binding::DetachSides, .Key = Keys::D, .Shift = true });
        Bindings::Add({ .Binding = Binding::DetachPoints, .Key = Keys::D, .Shift = true, .Control = true });

        Bindings::Add({ .Binding = Binding::SplitSegment2, .Key = Keys::S, .Shift = true });
        Bindings::Add({ .Binding = Binding::NewLevel, .Key = Keys::N, .Control = true });
        Bindings::Add({ .Binding = Binding::InvertMarked, .Key = Keys::I, .Control = true });
    }

    string GetShortcut(Binding bind) {
        for (auto& binding : _bindings) {
            if (binding.Binding == bind) return binding.GetShortcutLabel();
        }

        for (auto& binding : _realtimeBindings) {
            if (binding.Binding == bind) return binding.GetShortcutLabel();
        }

        return {};
    }

}
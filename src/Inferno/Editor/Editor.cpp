#include "pch.h"
#include <lodepng.h>
#include "Editor.h"
#include "Input.h"
#include "Bindings.h"
#include "Gizmo.h"
#include "WindowsDialogs.h"
#include "Game.h"
#include "Editor.Object.h"
#include "imgui_local.h"
#include "Editor.Texture.h"
#include "Editor.Segment.h"
#include "Editor.Wall.h"
#include "Convert.h"
#include "Editor.Clipboard.h"
#include "Editor.Geometry.h"
#include "Editor.IO.h"
#include "Editor.Undo.h"
#include "Game.Object.h"
#include "Version.h"
#include "Game.Segment.h"
#include "GameTimer.h"
#include "Resources.h"
#include "logging.h"

namespace Inferno::Editor {
    using Input::SelectionState;

    void SetMode(SelectionMode mode) {
        // Cycle selection on subsequent presses
        if (Settings::Editor.SelectionMode == mode) {
            if (Input::ShiftDown)
                Editor::Selection.PreviousItem();
            else
                Editor::Selection.NextItem();
        }

        Settings::Editor.SelectionMode = mode;

        switch (mode) {
            case SelectionMode::Object:
                Events::SelectObject();
                break;
            default:
                Events::SelectSegment();
        }

        Editor::Gizmo.UpdateAxisVisiblity(mode);
    }

    void ToggleTextureMode() {
        Settings::Editor.EnableTextureMode = !Settings::Editor.EnableTextureMode;
        Editor::Gizmo.UpdateAxisVisiblity(Settings::Editor.SelectionMode);
        Editor::Gizmo.UpdatePosition();
    }

    Editor::SelectionMode GetMode() { return Settings::Editor.SelectionMode; }

    // Selects a nearby segment that isn't in the marked items
    void UpdateSelectionAfterDelete(span<SegID> marked) {
        SegID nearby = Editor::Selection.Segment;

        if (Seq::contains(marked, nearby)) {
            if (auto seg = Game::Level.TryGetSegment(nearby)) {
                for (auto& conn : seg->Connections) {
                    if (conn == SegID::None) continue;
                    if (!Seq::contains(marked, conn)) nearby = conn;
                }
            }
        }

        // Adjust for deleted segments
        for (auto& mark : marked)
            if (mark <= nearby) nearby--;

        Editor::Selection.SetSelection(nearby);
    }


    void OnDelete() {
        if (Editor::Gizmo.State == GizmoState::Dragging) return;
        Editor::History.SnapshotSelection();

        switch (Settings::Editor.SelectionMode) {
            case SelectionMode::Object:
            {
                auto newSelection = Selection.Object;
                auto objs = Seq::ofSet(Editor::Marked.Objects);
                if (objs.empty())
                    objs.push_back(Selection.Object);

                Seq::sortDescending(objs);
                for (auto& obj : objs) {
                    if (obj < Selection.Object) newSelection--;
                    DeleteObject(Game::Level, obj);
                }

                Selection.SetSelection(newSelection);
                Editor::Marked.Objects.clear();
                Editor::History.SnapshotLevel("Delete Object(s)");
                Editor::History.SnapshotSelection();
                SetStatusMessage("Deleted {} object(s)", objs.size());
            }
            break;

            case SelectionMode::Segment:
            {
                auto segs = GetSelectedSegments();
                UpdateSelectionAfterDelete(segs);
                DeleteSegments(Game::Level, segs);

                Editor::Marked.Segments.clear();
                Editor::History.SnapshotLevel("Delete Segments");
                Editor::History.SnapshotSelection();
                SetStatusMessage("Deleted {} segments", segs.size());
            }
            break;

            default:
                if (auto newSelection = TryDeleteSegment(Game::Level, Selection.Segment)) {
                    Selection.SetSelection(newSelection);
                    PruneVertices(Game::Level);
                    Editor::History.SnapshotLevel("Delete Segment");
                    Editor::History.SnapshotSelection();
                }
                break;
        }
        Events::LevelChanged();
    }

    void OnInsert() {
        if (Editor::Gizmo.State == GizmoState::Dragging) return;

        switch (Settings::Editor.SelectionMode) {
            case SelectionMode::Object:
                Commands::AddObject();
                break;

            default:
                if (!Editor::Marked.Faces.empty() && Settings::Editor.SelectionMode == SelectionMode::Face) {
                    Commands::ExtrudeFaces();
                }
                else {
                    switch (Settings::Editor.InsertMode) {
                        case InsertMode::Normal: Commands::InsertSegment();
                            break;
                        case InsertMode::Extrude: Commands::ExtrudeSegment();
                            break;
                        case InsertMode::Mirror: Commands::InsertMirrored();
                            break;
                    }
                }
                break;
        }
    }

    void UpdateSelections(Level& level) {
        if (Input::ControlDown || Input::ShiftDown) {
            Editor::Marked.Update(level, MouseRay);
            if (Settings::Editor.SelectMarkedSegment && Input::ControlDown && Input::ShiftDown)
                Editor::Selection.Click(level, MouseRay, Settings::Editor.SelectionMode, false);
        }
        else {
            // Override the selection mode when alt is held down. Also enable invisible hit testing.
            auto mode = Input::AltDown ? SelectionMode::Face : Settings::Editor.SelectionMode;
            Editor::Selection.Click(level, MouseRay, mode, Input::AltDown);
        }
    }

    CursorDragMode BeginRightClickDrag(Level& level) {
        if (Settings::Editor.SelectionMode == SelectionMode::Face &&
            !Settings::Editor.EnableTextureMode) {
            Editor::History.SnapshotSelection();
            if (BeginExtrude(level))
                return CursorDragMode::Extrude;
        }
        else if (Settings::Editor.SelectionMode == SelectionMode::Object) {
            List<ObjID> newObjects;
            for (auto& id : GetSelectedObjects()) {
                if (auto obj = level.TryGetObject(id)) {
                    newObjects.push_back((ObjID)level.Objects.size());
                    level.Objects.push_back(*obj);
                }
            }

            Editor::Marked.Objects.clear();

            for (auto& id : newObjects)
                Editor::Marked.Objects.insert(id);

            return CursorDragMode::Transform;
        }
        else if (Settings::Editor.SelectionMode == SelectionMode::Segment &&
            !Settings::Editor.EnableTextureMode) {
            Editor::History.SnapshotSelection();
            auto segs = GetSelectedSegments();
            auto copy = CopySegments(level, segs);
            PasteSegmentsInPlace(level, copy);
            return CursorDragMode::Transform;
        }

        return DragMode; // no change
    }

    CursorDragMode UpdateGizmoDragState() {
        switch (Editor::Gizmo.State) {
            case GizmoState::BeginDrag:
                if (Settings::Editor.SelectionMode == SelectionMode::Object)
                    Editor::History.SnapshotSelection();

                if (Input::RightDragState == SelectionState::BeginDrag) {
                    return BeginRightClickDrag(Game::Level);
                }
                else if (Input::LeftDragState == SelectionState::BeginDrag) {
                    Editor::History.SnapshotSelection();
                    return CursorDragMode::Transform;
                }
                break;

            case GizmoState::Dragging:
                return DragMode; // no change

            case GizmoState::EndDrag:
                if (DragMode == CursorDragMode::Extrude) {
                    if (Editor::FinishExtrude(Game::Level, Editor::Gizmo)) {
                        Editor::History.SnapshotLevel("Extrude");
                        Events::LevelChanged();
                    }
                    else {
                        Editor::History.Restore();
                        SetStatusMessage("Tried to extrude a zero length segment");
                    }
                }
                else if (DragMode == CursorDragMode::Transform) {
                    if (Settings::Editor.SelectionMode == SelectionMode::Object)
                        Editor::History.SnapshotLevel("Transform Object");
                    else if (Settings::Editor.EnableTextureMode)
                        Editor::History.SnapshotLevel("Transform Texture");
                    else
                        Editor::History.SnapshotLevel("Transform Level");
                }

                return CursorDragMode::Select;
        }

        return CursorDragMode::Select;
    }

    // Enables mouselook while middle mouse is down
    void CheckForMouselook() {
        auto mouselookKey = Bindings::Active.GetBindingKey(EditorAction::HoldMouselook);

        if (Input::IsMouseButtonPressed(Input::MouseButtons::MiddleClick) ||
            Input::IsKeyPressed(mouselookKey)) {
            auto mode = Settings::Editor.MiddleMouseMode == MiddleMouseMode::Mouselook ? Input::MouseMode::Mouselook : Input::MouseMode::Orbit;
            Input::SetMouseMode(mode);
        }

        if (Input::IsMouseButtonReleased(Input::MouseButtons::MiddleClick) ||
            Input::IsKeyReleased(mouselookKey))
            Input::SetMouseMode(Input::MouseMode::Normal);
    }

    void CreateMatcenEffects(const Level& level) {
        static GameTimer matcenTimer(0);
        if (matcenTimer > 0) return;

        auto& vclip = Resources::GetVideoClip(VClipID::Matcen);
        matcenTimer = vclip.PlayTime + 2.0f;

        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.Segments[id];
            if (seg.Type == SegmentType::Matcen)
                CreateMatcenEffect(level, (SegID)id);
        }
    }

    // Checks if the current file is dirty. Returns true if the file can be closed.
    bool CanCloseCurrentFile() {
        if (!Editor::History.Dirty() || Game::Level.IsShareware || Game::GetState() != GameState::Editor)
            return true;

        auto file = Game::Mission ? Game::Mission->Path.filename().wstring() : Convert::ToWideString(Game::Level.FileName);
        auto msg = fmt::format(L"Do you want to save changes to {}?", file);
        auto result = ShowYesNoCancelMessage(msg.c_str(), L"File has changed");
        if (!result) return false; // Cancelled
        if (*result) Editor::Commands::Save(); // Yes
        return true;
    }

    bool ImGuiHadMouseFocus = false;

    void Update() {
        // don't do anything when a modal is open
        if (ImGui::GetTopMostPopupModal()) return;

        CheckForMouselook();

        auto& level = Game::Level;
        auto& io = ImGui::GetIO();
        MouseRay = Editor::EditorCamera.UnprojectRay(Input::MousePosition * Settings::Graphics.RenderScale);

        if (!io.WantCaptureMouse && ImGuiHadMouseFocus) {
            // Reset the keyboard state so holding a key down while moving the mouse
            // off the UI doesn't cause the key to get stuck
            io.ClearInputKeys();
        }

        ImGuiHadMouseFocus = io.WantCaptureMouse;

        if (Settings::Editor.ShowMatcenEffects)
            CreateMatcenEffects(Game::Level);

        // Only execute keyboard bindings if text focus is not in imgui
        if (!io.WantTextInput)
            Bindings::Update();

        // selection state is determined first, then gizmo, then cursor
        Editor::Gizmo.Update(Input::DragState, Editor::EditorCamera);

        // Cancel right click drags on global transform mode and texture mode
        if ((Settings::Editor.SelectionMode == SelectionMode::Transform || Settings::Editor.EnableTextureMode) &&
            Editor::Gizmo.State == GizmoState::Dragging && io.MouseDown[1])
            Editor::Gizmo.CancelDrag();

        // only update mouse functionality if mouse not over imgui
        if (ImGui::GetCurrentContext()->HoveredWindow && Input::GetMouseMode() == Input::MouseMode::Normal)
            return;

        DragMode = UpdateGizmoDragState();

        switch (DragMode) {
            case CursorDragMode::Select:
                break;
            case CursorDragMode::Extrude:
                TransformSelection(level, Editor::Gizmo);
                UpdateExtrudes(level, Editor::Gizmo);
                Events::LevelChanged();
                break;
            case CursorDragMode::Transform:
                TransformSelection(level, Editor::Gizmo);
                Events::LevelChanged();
                break;
        }

        if (Editor::Gizmo.State == GizmoState::RightClick && Settings::Editor.EnableTextureMode) {
            if (Editor::Gizmo.SelectedAxis == GizmoAxis::X) Commands::FlipTextureV();
            if (Editor::Gizmo.SelectedAxis == GizmoAxis::Y) Commands::FlipTextureU();
            if (Editor::Gizmo.SelectedAxis == GizmoAxis::Z) Commands::RotateOverlay();
        }
        else if (Editor::Gizmo.State == GizmoState::None || Editor::Gizmo.State == GizmoState::LeftClick) {
            // Only do selection stuff when the gizmo isn't being dragged.
            // Also allow gizmo left clicks to pass through.
            switch (Input::LeftDragState) {
                case SelectionState::Released:
                    UpdateSelections(level);
                    break;
                case SelectionState::ReleasedDrag:
                    Editor::Marked.UpdateFromWindow(level, Input::DragStart, Input::MousePosition, Editor::EditorCamera);
                    Editor::Gizmo.UpdatePosition();
                    break;
            }

            if (Input::GetMouseMode() != Input::MouseMode::Normal || Input::LeftDragState == SelectionState::None)
                UpdateCamera(Editor::EditorCamera); // Only allow camera movement when not dragging unless in mouselook mode
        }

        CheckForAutosave();
    }

    void AlignUserCSysToGizmo() {
        UserCSys = Editor::Gizmo.Transform;
    }

    void AlignUserCSysToSide() {
        if (!Game::Level.SegmentExists(Selection.Segment)) return;
        //auto& seg = Game::Level->GetSegment(Selection.Segment);
        auto face = Face::FromSide(Game::Level, Selection.Segment, Selection.Side);
        auto v0 = face.VectorForEdge(Selection.Point - 1);
        auto v1 = -face.VectorForEdge(Selection.Point + 1);
        auto average = (v0 + v1) / 2;
        average.Normalize();
        auto up = face.AverageNormal();
        auto right = average.Cross(up);
        right.Normalize();
        UserCSys.Forward(average);
        UserCSys.Right(right);
        UserCSys.Up(up);
        UserCSys.Translation(face.Center());
        Editor::Gizmo.UpdatePosition();
    }

    void AlignUserCSysToMarked() {
        Vector3 center;
        auto indices = GetSelectedVertices();

        for (auto& index : indices) {
            if (auto v = Game::Level.TryGetVertex(index))
                center += *v;
        }

        if (indices.empty()) return;
        center /= (float)indices.size();
        UserCSys.Translation(center);
        Editor::Gizmo.UpdatePosition();
    }

    void ExportBitmap(LevelTexID id) {
        auto& lti = Resources::GetLevelTextureInfo(id);
        if (lti.EffectClip != EClipID::None) {
            auto& eclip = Resources::GetEffectClip(lti.EffectClip);
            for (auto& frame : eclip.VClip.GetFrames()) {
                auto& bmp = Resources::GetBitmap(frame);
                SPDLOG_INFO("Exporting {}", bmp.Info.Name);
                std::filesystem::remove(bmp.Info.Name + ".png");
                lodepng::encode(bmp.Info.Name + ".png", (ubyte*)bmp.Data.data(), bmp.Info.Width, bmp.Info.Height);
            }
        }
        else {
            auto& bmp = Resources::GetBitmap(lti.TexID);
            SPDLOG_INFO("Exporting {}", bmp.Info.Name);
            lodepng::encode(bmp.Info.Name + ".png", (ubyte*)bmp.Data.data(), bmp.Info.Width, bmp.Info.Height);
        }
        //lodepng::encode("st/" + bmp.Name + ".png", (ubyte*)bmp.Data.data(), bmp.Width, bmp.Height);
        //lodepng::encode("st/" + bmp.Name + "_st.png", (ubyte*)bmp.Mask.data(), bmp.Width, bmp.Height);
    }

    void AlignViewToFace(const Level& level, Camera& camera, Tag tag, int point);

    void ZoomExtents(const Level& level, Camera& camera);

    void ResetFlickeringLightTimers(Level& level) {
        for (auto& light : level.FlickeringLights) {
            // timers are persisted and actually affect the in-game start time.
            // this could be used to precisely offset the flicker pattern
            light.Timer = 0;
        }
    }

    // Turns on all flickering lights and updates the view
    void DisableFlickeringLights(Level& level) {
        for (auto& light : level.FlickeringLights) {
            if (auto seg = level.TryGetSegment(light.Tag))
                Inferno::AddLight(level, light.Tag, *seg);
        }
    }

    void ResetObjects(Level& level) {
        int id = 0;
        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::Player) {
                // Reload player settings
                const auto& ship = Resources::GameData.PlayerShip;
                auto& physics = obj.Physics;
                physics.Brakes = physics.TurnRoll = 0;
                physics.Drag = ship.Drag;
                physics.Mass = ship.Mass;

                physics.Flags |= PhysicsFlag::TurnRoll | PhysicsFlag::AutoLevel | PhysicsFlag::Wiggle | PhysicsFlag::UseThrust;

                obj.Render.Model.ID = ship.Model;
                obj.Render.Model.subobj_flags = 0;
                obj.Render.Model.TextureOverride = LevelTexID::None;
                for (auto& angle : obj.Render.Model.Angles)
                    angle = Vector3::Zero;

                obj.Flags = (ObjectFlag)0;
            }

            if (obj.Type == ObjectType::Robot && Seq::inRange(Resources::GameData.Robots, obj.ID)) {
                auto& physics = obj.Physics;
                auto& robot = Resources::GameData.Robots[obj.ID];
                physics.Mass = robot.Mass;
                physics.Drag = robot.Drag;
            }

            if (obj.Contains.Type == ObjectType::None && obj.Contains.Count > 0)
                SPDLOG_WARN("Object {} has a contains count > 0 with no type. Resave to fix.", id);

            id++;
        }
    }

    List<PointID> GetSelectedVertices() {
        auto verts = Editor::Marked.GetVertexHandles(Game::Level);
        if (verts.empty())
            verts = Editor::Selection.GetVertexHandles(Game::Level);

        return verts;
    }

    List<Tag> GetSelectedFaces() {
        auto faces = Editor::Marked.GetMarkedFaces();
        if (faces.empty()) {
            if (Settings::Editor.SelectionMode == SelectionMode::Segment) {
                for (auto& side : SIDE_IDS)
                    faces.push_back({ Editor::Selection.Segment, side });
            }
            else {
                faces.push_back(Editor::Selection.Tag());
            }
        }

        return faces;
    }

    List<WallID> GetSelectedWalls() {
        List<WallID> walls;
        for (auto& id : GetSelectedFaces()) {
            auto wall = Game::Level.GetWallID(id);
            if (wall != WallID::None)
                walls.push_back(wall);
        }

        return walls;
    }

    void CheckTriggers(Level& level) {
        for (int tid = 0; tid < level.Triggers.size(); tid++) {
            auto& trigger = level.Triggers[tid];

            // In reverse to preserve order while removing
            for (int i = (int)trigger.Targets.Count() - 1; i >= 0; i--) {
                auto& target = trigger.Targets[i];
                if (target.Side > SideID::Front || target.Side <= SideID::None) {
                    SPDLOG_WARN("Removing invalid trigger target {}:{} from trigger {}", target.Segment, target.Side, tid);
                    trigger.Targets.Remove(i);
                }
            }
        }
    }

    void OnLevelLoad(bool reload) {
        auto& level = Game::Level;
        if (!reload) {
            if (level.CameraUp != Vector3::Zero) {
                Editor::EditorCamera.Position = level.CameraPosition;
                Editor::EditorCamera.Target = level.CameraTarget;
                Editor::EditorCamera.Up = level.CameraUp;
            }
            else {
                Commands::ZoomExtents();
            }
        }

        auto seg = level.Segments.empty() ? SegID::None : SegID(0);
        Editor::Selection.Object = level.Objects.empty() ? ObjID::None : ObjID(0);
        Editor::Marked.Clear();

        Editor::Selection.SetSelection({ seg, SideID::Left });
        Editor::History = { &level, Settings::Editor.UndoLevels };
        UpdateSecretLevelReturnMarker();
        ResetFlickeringLightTimers(level);
        ResetObjects(level);

        for (auto& obj : level.Objects)
            obj.Radius = GetObjectRadius(obj);

        CheckTriggers(level);
        Editor::Events::LevelLoaded();
        SetStatusMessage("Loaded level with {} segments and {} vertices", level.Segments.size(), level.Vertices.size());

        if (!Resources::HasGameData())
            Events::ShowDialog(DialogType::Settings);

        ResetAutosaveTimer();
    }

    Segment* GetSelectedSegment() {
        return Game::Level.TryGetSegment(Editor::Selection.Segment);
    }

    void CleanLevel(Level& level) {
        if (PruneVertices(level))
            Marked.Points.clear();

        for (int tid = (int)level.Triggers.size() - 1; tid >= 0; tid--) {
            int count = 0;

            // Check if any walls own this trigger
            for (auto& wall : level.Walls) {
                if (wall.Trigger == (TriggerID)tid) count++;
            }

            if (count == 0) {
                Editor::RemoveTrigger(level, (TriggerID)tid);
                SPDLOG_WARN("Removed unused trigger {}", tid);
            }

            if (count > 1) {
                SPDLOG_WARN("Trigger {} belongs to more than one wall", tid);
            }
        }
    }

    void InitEditor() {
        Events::SelectTexture += OnSelectTexture;
        Events::LevelLoaded += [] { Editor::Gizmo.UpdatePosition(); };
        Events::SelectObject += [] { Editor::Gizmo.UpdatePosition(); };
        Events::SelectSegment += [] { Editor::Gizmo.UpdatePosition(); };
        Events::LevelChanged += [] { Editor::Gizmo.UpdatePosition(); };

        Editor::Bindings::LoadDefaults();
    }

    void OpenRecentOrEmpty() {
        if (Settings::Editor.ReopenLastLevel &&
            !Settings::Editor.RecentFiles.empty() &&
            filesystem::exists(Settings::Editor.RecentFiles.front())) {
            Game::LoadLevel(Settings::Editor.RecentFiles.front(), "", true);
        }
        else {
            int16 version = 7; // Default to D2 levels
            if (Resources::FoundDescent1() && !Resources::FoundDescent2()) version = 1; // User only has D1 and not D2
            NewLevelInfo info = { .Title = "new level", .FileName = "new", .Version = version, .AddToHog = false };
            Game::NewLevel(info);
        }
    }

    filesystem::path GetGameExecutablePath() {
        return Game::Level.IsDescent1() ? Settings::Inferno.Descent1Path : Settings::Inferno.Descent2Path;
    }

    namespace Commands {
        Command AlignViewToFace{
            .Action = [] { Editor::AlignViewToFace(Game::Level, Editor::EditorCamera, Selection.Tag(), Selection.Point); },
            .Name = "Align View to Face"
        };

        Command ZoomExtents{
            .Action = [] { Editor::ZoomExtents(Game::Level, Editor::EditorCamera); },
            .Name = "Zoom Extents"
        };

        void CleanLevel() {
            Editor::CleanLevel(Game::Level);
            Editor::History.SnapshotLevel("Clean level");
        }

        void GoToReactor() {
            if (auto reactor = Seq::findIndex(Game::Level.Objects, IsReactor)) {
                Selection.SetSelection((ObjID)*reactor);
                FocusObject();
            }
            else {
                SetStatusMessageWarn("No reactor in level");
            }
        }

        void Commands::GoToBoss() {
            if (auto boss = Seq::findIndex(Game::Level.Objects, IsBossRobot)) {
                Selection.SetSelection((ObjID)*boss);
                FocusObject();
            }
            else {
                SetStatusMessageWarn("No boss in level");
            }
        }

        void GoToPlayerStart() {
            if (auto id = Seq::findIndex(Game::Level.Objects, IsPlayer)) {
                Selection.SetSelection((ObjID)*id);
                FocusObject();
            }
            else {
                SetStatusMessageWarn("No player in level");
            }
        }

        void GoToExit() {
            if (auto exit = FindExit(Game::Level)) {
                Selection.SetSelection(exit);
                FocusSegment();
                return;
            }

            SetStatusMessageWarn("No exit in level");
        }

        void GoToSecretExit() {
            if (auto tid = Seq::findIndex(Game::Level.Triggers, [](const Trigger& trigger) { return IsSecretExit(Game::Level, trigger); })) {
                if (auto wall = Game::Level.TryGetWall((TriggerID)*tid)) {
                    Selection.SetSelection(wall->Tag);
                    FocusSegment();
                    return;
                }
            }

            SetStatusMessageWarn("No secret exit in level");
        }

        void GoToSecretExitReturn() {
            if (!Game::Level.HasSecretExit()) {
                SetStatusMessageWarn("No secret exit in level");
                return;
            }

            Selection.SetSelection(Game::Level.SecretExitReturn);
            FocusSegment();
        }


        void StartGame() {
            // Start the child process. 
            STARTUPINFO si{};
            PROCESS_INFORMATION pi{};

            if (!CreateProcess(GetGameExecutablePath().c_str(),
                               nullptr, // Command line
                               nullptr, // Process handle not inheritable
                               nullptr, // Thread handle not inheritable
                               false, // Set handle inheritance to FALSE
                               0, // No creation flags
                               nullptr, // Use parent's environment block
                               GetGameExecutablePath().parent_path().c_str(), // Starting directory 
                               &si,
                               &pi)) {
                auto msg = fmt::format("Unable to start game:\n{}", GetLastError());
                ShowErrorMessage(Convert::ToWideString(msg));
                return;
            }

            //WaitForSingleObject(pi.hProcess, INFINITE);
            // Closing handle does not close game, but does clean up properly
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            //SetStatusMessage("Game exited");
        }

        void PlaytestLevel() {
            try {
                filesystem::path exe = GetGameExecutablePath();
                auto missionFolder = exe.parent_path() / "missions";
                auto mission = Game::Mission ? &Game::Mission.value() : nullptr;
                WritePlaytestLevel(missionFolder, Game::Level, mission);
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        void Exit() {
            PostMessage(Shell::Hwnd, WM_CLOSE, 0, 0);
        }

        Command Insert{ .Action = OnInsert, .Name = "Insert" };
        Command Delete{ .Action = OnDelete, .Name = "Delete" };
        Command Undo{ .Action = [] { History.Undo(); }, .CanExecute = [] { return History.CanUndo(); }, .Name = "Undo" };
        Command Redo{ .Action = [] { History.Redo(); }, .CanExecute = [] { return History.CanRedo(); }, .Name = "Redo" };

        Command CycleRenderMode{
            .Action = [] {
                int i = (int)Settings::Editor.RenderMode + 1;
                if (i > (int)RenderMode::Shaded) {
                    i = 0;
                    Settings::Editor.ShowWireframe = true;
                }
                else {
                    Settings::Editor.ShowWireframe = false;
                }

                Settings::Graphics.NewLightMode = i != (int)RenderMode::Textured;
                Settings::Editor.RenderMode = (RenderMode)i;
            },
            .Name = "Cycle Render Mode"
        };

        Command DisableFlickeringLights{ .Action = [] { Editor::DisableFlickeringLights(Game::Level); } };

        Command ToggleWireframe{
            .Action = [] {
                Settings::Editor.ShowWireframe = !Settings::Editor.ShowWireframe;

                // Always keep something visible
                if (!Settings::Editor.ShowWireframe && Settings::Editor.RenderMode == RenderMode::None)
                    Settings::Editor.RenderMode = RenderMode::Textured;
            },
            .Name = "Toggle Wireframe"
        };
    }
}
